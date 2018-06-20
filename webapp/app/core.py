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
                statusrow = database.get_most_recent(db, 'status', {'battery': battery})
                if statusrow:
                    batteries[battery]['status'] = database.status_row_to_message(statusrow)
    app.logger.info("Startup state: %s", batteries)

def update_timeout(config):
    now = datetime.now()
    timeElapsed = (now - config['timestamp']).total_seconds() / 60
    return max(0, int(config['manualTimeout'] - timeElapsed))

def status_matches_config(status, config):
    # Make sure that the config row has raw values to compare. This is
    # needed when the config is loaded from the database, but also
    # makes sure that changing the calibration values invalidates the
    # config and makes sure a new one is sent.
    # TODO: Should this really call this mqtt function directly?
    mqtt.calibrate_config(app, config['battery'], config)

    expectedTimeout = update_timeout(config)
    app.logger.debug("Timeout expected: %s, received %s. Original timeout: %s", expectedTimeout, status['manualTimeout'], config['manualTimeout'])
    margin = 2 # minutes
    if (status['manualTimeout'] < expectedTimeout - margin
            or status['manualTimeout'] > expectedTimeout + margin):
        return False

    fields = ['targetFlow', 'targetLevelRaw', 'minLevelRaw', 'maxLevelRaw']

    # Only check pump state when in manual mode
    if status['manualTimeout']:
        fields.append('pump')

    # Check the relevant fields for equality
    ok = all(status[f] == config[f] for f in fields)

    return ok

def status_for_battery(battery):
    return batteries[battery]['status']

def config_for_battery(battery):
    return batteries[battery]['config']

def process_uplink(status):
    status['timestamp'] = datetime.now()

    app.logger.debug("Received status: %s", status)
    values = database.status_message_to_row(status)

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
                    database.update_from_dict(db, 'config', {'id': config['id']}, {'ackTimestamp': now})
                    config['ackTimestamp'] = now
                    websocket.send_config(config)
    websocket.send_status(status, battery)

def process_command(config):
    config.update({
      'timestamp': datetime.now(),
      'ackTimestamp': None,
      'username': flask_user.current_user.username,
    })

    app.logger.info('Received command: %s', config)
    db = app.get_db()
    v = database.config_message_to_row(config)

    # Insert into database
    cur = database.insert_from_dict(db, 'config', v)
    db.commit()
    config['id'] = cur.lastrowid

    # Update last-known config
    batteries[config['battery']]['config'] = config
    # Send config to node
    mqtt.send_command(app, config)

    websocket.reply_message('Commando wordt zo snel mogelijk verstuurd')
    websocket.send_config(config)

# vim: set sts=4 sw=4 expandtab:
