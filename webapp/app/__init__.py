# all the imports
import os
from datetime import datetime
from flask import Flask, request, session, g, redirect, url_for, abort, \
     render_template, flash
from flask_socketio import SocketIO, send, emit
from threading import Thread
from jinja2.parser import *

from . import mqtt, database


app = Flask(__name__)
app.config.from_object(__name__) # load config from this file , flaskr.py
app.socketio = SocketIO(app)

# Defaults
app.config.update(dict(
    DATABASE=os.path.join(app.root_path, 'app.db'),
    TTN_HOST='eu.thethings.network',
    TTN_CA_CERT_PATH='mqtt-ca.pem',
    TTN_APP_ID='lankheet',
    TTN_PORT=8883,
    ERRORS_FROM='matthijs@stdin.nl',
    DEVICES={
        # Maps TTN device id to list of connected battery ids
        'stuurkast-3': ['lankheet-1', 'lankheet-2'],
    }
))
# Load config.py
app.config.from_object('config')

if not app.debug and app.config.get('ERRORS_TO', []):
    import logging
    from logging.handlers import SMTPHandler
    mail_handler = SMTPHandler('127.0.0.1',
                               app.config['ERRORS_FROM'],
                               app.config['ERRORS_TO'],
                               'Lankheet error')
    mail_handler.setLevel(logging.ERROR)
    app.logger.addHandler(mail_handler)

@app.route('/')
def index():
    context = {
        'login': True,
        'id': 'lankheet-1',
    }
    try:
        return render_template('index.html', **context)
    except TemplateSyntaxError as e:
        print(e.lineno)


def send_message(message):
    send(message)

@app.socketio.on_error()
def handle_error(e):
    send_message('Fout in afhandeling: ' + str(e))
    raise e

# {"battery":0, "manualTimeout":0,"pump":[true,false,false,true],"targetFlow":"40","targetLevel":[30,45,60],"minLevel":[0,0,0],"maxLevel":[80,130,180]}
@app.socketio.on('command')
def handle_message(cmd):
    print('received command: ' + str(cmd))
    db = app.get_db()
    v = database.config_message_to_row(cmd)
    v.update({
      'timestamp': datetime.now(),
      'ackTimestamp': None,
    })

    cur = database.insert_from_dict(db, 'config', v)
    db.commit()
    send_message('Commando wordt zo snel mogelijk verstuurd')
    mqtt.send_command(app, cmd)

database.setup_db(app)
# This also runs the mqtt thread when doing e.g. initdb, but I could not
# find an easy way around this.
mqtt.run(app)

# vim: set sts=4 sw=4 expandtab:
