from datetime import datetime

from . import mqtt, websocket, database, app

def process_uplink(status):
    app.logger.debug("Received status: %s", status)
    values = database.status_message_to_row(status)
    values['timestamp'] = datetime.now()
    with app.app_context():
        db = app.get_db()
        database.insert_from_dict(db, 'status', values)
    websocket.send_status(status)

def process_command(cmd):
    print('Received command: ' + str(cmd))
    db = app.get_db()
    v = database.config_message_to_row(cmd)
    v.update({
      'timestamp': datetime.now(),
      'ackTimestamp': None,
    })

    cur = database.insert_from_dict(db, 'config', v)
    db.commit()
    mqtt.send_command(app, cmd)
    websocket.reply_message('Commando wordt zo snel mogelijk verstuurd')
