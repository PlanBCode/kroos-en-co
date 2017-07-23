# all the imports
import os
import flask

app = flask.Flask(__name__)

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

# Import these at the end, so they can access a completely setup
# core.app
from . import mqtt, database, web, websocket

core.setup()

# This also runs the mqtt thread when doing e.g. initdb, but I could not
# find an easy way around this.
mqtt.run(app)

# vim: set sts=4 sw=4 expandtab:
