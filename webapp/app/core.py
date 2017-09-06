from datetime import datetime
import flask_user

from . import mqtt, websocket, database, app

batteries = {}


def setup():
    with app.app_context():
        db = app.get_db()
        for dev, bats in app.config['DEVICES'].items():
            for battery in bats:
                batteries[battery] = {
                    'status': None,
                    'config': None,
                }
                configrow = database.get_most_recent(db, 'config', {'battery': battery})
                if configrow:
                    batteries[battery]['config'] = database.config_row_to_message(configrow)
    app.logger.info("Startup state: %s", batteries)

def update_timeout(config):
    now = datetime.now()
    timeElapsed = (now - config['timestamp']).total_seconds() / 60
    return max(0, int(config['manualTimeout'] - timeElapsed))

def status_matches_config(status, config):
    expectedTimeout = update_timeout(config)
    app.logger.debug("Timeout expected: %s, received %s. Original timeout: %s", expectedTimeout, status['manualTimeout'], config['manualTimeout'])
    margin = 2 # minutes
    if (status['manualTimeout'] < expectedTimeout - margin
            or status['manualTimeout'] > expectedTimeout + margin):
        return False

    fields = ['targetFlow', 'targetLevel', 'minLevel', 'maxLevel']

    # Only check pump state when in manual mode
    if status['manualTimeout']:
        fields.append('pump')

    # Check the relevant fields for equality
    ok = all(status[f] == config[f] for f in fields)

    return ok

def status_for_battery(battery):
    return batteries[battery]['status']

def process_uplink(status):
    app.logger.debug("Received status: %s", status)
    values = database.status_message_to_row(status)
    values['timestamp'] = datetime.now()

    battery = status['battery']
    prev_status = batteries[battery]['status']
    if status['panic'] and prev_status and not prev_status['panic']:
        app.logger.error("Panic mode enabled: {}".format(status))

    with app.app_context():
        db = app.get_db()
        database.insert_from_dict(db, 'status', values)
        batteries[battery]['status'] = status

        # See if the status matches the current config, and if not resend
        # the config
        config = batteries[battery]['config']

        if config:
            if not status_matches_config(status, config):
                # Config does not match, resend
                # First update timeout to subtract elapsed time
                config = dict(config)
                config['manualTimeout'] = update_timeout(config)
                mqtt.send_command(app, config)
                if config['ackTimestamp']:
                    app.logger.warn("Received config does not match, but config was previously acked")
                    app.logger.warn("Received: %s", status)
                    app.logger.warn("Expected: %s", config)
            else:
                # Config matches, note it has been acked if not already
                if not config['ackTimestamp']:
                    now = datetime.now()
                    database.update_from_dict(db, 'config', {'ackTimestamp': now})
                    config['ackTimestamp'] = now
    websocket.send_status(status, battery)

def process_command(cmd):
    app.logger.info('Received command: %s', cmd)
    db = app.get_db()
    v = database.config_message_to_row(cmd)
    v.update({
      'timestamp': datetime.now(),
      'ackTimestamp': None,
      'username': flask_user.current_user.username,
    })

    # Insert into database
    cur = database.insert_from_dict(db, 'config', v)
    db.commit()
    # Update last-known config
    batteries[cmd['battery']]['config'] = database.config_row_to_message(v)
    # Send config to node
    mqtt.send_command(app, cmd)

    websocket.reply_message('Commando wordt zo snel mogelijk verstuurd')

# vim: set sts=4 sw=4 expandtab:
