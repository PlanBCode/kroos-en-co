from flask_socketio import SocketIO, send, emit, join_room
import flask_user

from . import core, app

app.socketio = SocketIO(app)

@app.socketio.on_error()
def handle_error(e):
    reply_message('Fout in afhandeling: ' + str(e))
    raise e

@app.socketio.on('select_battery')
def handle_select_battery(msg):
    battery = msg['battery']
    app.logger.debug("Selected battery: %s", battery)
    # Subscribe to all future status updates for this battery
    join_room(battery)

@app.socketio.on('command')
def handle_message(cmd):
    if not flask_user.access.is_authenticated():
        reply_message("Command failed, you must be logged in")
    else:
        core.process_command(cmd)

def reply_message(message):
    send(message)

def send_status(status, battery):
    app.logger.debug("Broadcasting status for %s: %s", battery, str(status))
    app.socketio.emit('status', status, room=battery)

