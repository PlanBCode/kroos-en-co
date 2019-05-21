from flask_socketio import SocketIO, send, emit, join_room
import flask_user

from . import core, app

app.socketio = SocketIO(app)

@app.socketio.on_error()
def handle_error(e):
    reply_message('Fout in afhandeling: ' + str(e))
    raise e

def convert_timestamp(info):
    """
    Convert the timestamp element in a status or config dict to
    string. Returns a copy of the dict passed.
    """
    info = dict(info)
    for key in ('timestamp', 'ackTimestamp'):
        timestamp = info.get(key, None)
        if timestamp:
            info[key] = timestamp.isoformat()
    return info

@app.socketio.on('select_battery')
def handle_select_battery(msg):
    battery = msg['battery']
    app.logger.debug("Selected battery: %s", battery)
    status = core.status_for_battery(battery)
    # Send the most recent status, if any
    if status:
        status = convert_timestamp(status)
        app.logger.debug("Sending initial status:\n%s", core.pp_obj(status))
        emit('status', status)
    config = core.config_for_battery(battery)
    # Send the most recent config, if any
    if config:
        config = convert_timestamp(config)
        app.logger.debug("Sending initial config:\n%s", core.pp_obj(config))
        emit('config', config)
    # And subscribe to all future status updates for this battery
    join_room(battery)

@app.socketio.on('command')
def handle_command(cmd):
    if not flask_user.access.is_authenticated():
        reply_message("Command failed, you must be logged in")
    else:
        core.process_command(cmd)

def reply_message(message):
    send(message)

def send_status(status, battery):
    app.logger.debug("Broadcasting status for %s:\n%s", battery, core.pp_obj(status))
    status = convert_timestamp(status)
    app.socketio.emit('status', status, room=battery)

def send_config(config):
    battery = config['battery']
    app.logger.debug("Broadcasting config for %s:\n%s", battery, core.pp_obj(config))
    config = convert_timestamp(config)
    app.socketio.emit('config', config, room=battery)

