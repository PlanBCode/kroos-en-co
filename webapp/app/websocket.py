from flask_socketio import SocketIO, send, emit

from . import core, app

app.socketio = SocketIO(app)

@app.socketio.on_error()
def handle_error(e):
    reply_message('Fout in afhandeling: ' + str(e))
    raise e

@app.socketio.on('command')
def handle_message(cmd):
    core.process_command(cmd)

def reply_message(message):
    send(message)

# TODO: Limit to clients for this battery
def send_status(status):
    app.logger.debug("Sending status: %s", str(status))
    app.socketio.emit('status', status, broadcast=True)

