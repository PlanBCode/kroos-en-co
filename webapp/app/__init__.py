# all the imports
import os
import sys
import flask
import flask_mail

app = flask.Flask(__name__)

# Defaults
app.config.update(dict(
    DATABASE=os.path.join(app.root_path, 'app.db'),
    SQLALCHEMY_DATABASE_URI = 'sqlite:///' + os.path.join(app.root_path, 'app.db'),
    SQLALCHEMY_TRACK_MODIFICATIONS = False,   # Avoids a SQLAlchemy Warning
    TTN_HOST='eu.thethings.network',
    TTN_CA_CERT_PATH='mqtt-ca.pem',
    TTN_APP_ID='lankheet',
    TTN_PORT=8883,
    DEVICES={
        # Maps TTN device id to list of connected battery ids
        'stuurkast-3': ['lankheet-1', 'lankheet-2'],
    },
    # Used by flask-user e-mail templates
    USER_APP_NAME="Lankheet krooscockpit",
    # Users need to be invited by other users to access the controls
    USER_ENABLE_INVITATION=True,
    USER_REQUIRE_INVITATION=True,
    USER_ENABLE_CHANGE_USERNAME=False,
    # Allow setting the SERVER_NAME on the commandline, to generate
    # links in outgoing e-mails
    SERVER_NAME=os.environ.get('FLASK_SERVER_NAME', None),
))

# Load config.py
try:
    app.config.from_object('config')
except ImportError as e:
    raise ImportError("Config file `config.py` not found. Copy `config.py.tmpl` to `config.py` and set values there.")

if app.config.get('ERRORS_TO', []):
    import logging
    from logging.handlers import SMTPHandler
    mail_handler = SMTPHandler('127.0.0.1',
                               app.config['ERRORS_FROM'],
                               app.config['ERRORS_TO'],
                               'Lankheet error')
    mail_handler.setLevel(logging.ERROR)
    app.logger.addHandler(mail_handler)
mail = flask_mail.Mail(app)

# Import these at the end, so they can access a completely setup
# core.app
from . import mqtt, database, web, websocket, auth

# This is a hack to prevent running these when doing "flask initdb". There
# seems to be no sane way to run a command only when actually running a server
# (using flask run or inside gunicorn or whatever), so this just checks for
# initdb explicitely.
if 'initdb' not in sys.argv and 'invite' not in sys.argv:
    core.setup()

    mqtt.run(app)

# vim: set sts=4 sw=4 expandtab:
