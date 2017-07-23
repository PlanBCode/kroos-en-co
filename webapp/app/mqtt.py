import paho.mqtt.client as mqtt
import threading
import json
import base64
import binascii

from . import core

def on_connect(client, userdata, flags, rc):
    app = userdata['app']
    app.logger.info('MQTT connected')
    client.subscribe('+/devices/+/up')
    #client.subscribe('+/devices/+/events/activations')
    #client.subscribe('+/devices/+/events/down/sent')

def on_disconnect(client, userdata, rc):
    app = userdata['app']
    app.logger.warn('MQTT disconnected')

def on_log(client, userdata, level, buf):
    app = userdata['app']

    if level == mqtt.MQTT_LOG_INFO:
        app.logger.info(buf)
    if level == mqtt.MQTT_LOG_NOTICE:
        app.logger.notice(buf)
    if level == mqtt.MQTT_LOG_WARNING:
        app.logger.warning(buf)
    if level == mqtt.MQTT_LOG_ERR:
        app.logger.error(buf)
    if level == mqtt.MQTT_LOG_DEBUG:
        app.logger.debug(buf)

def on_message(client, userdata, mqtt_msg):
    app = userdata['app']
    try:
        msg_as_string = mqtt_msg.payload.decode('utf8')
        msg = json.loads(msg_as_string)
        app.logger.debug("Received packet: " + str(msg))
        payload_raw = base64.b64decode(msg.get('payload_raw', ''))
    # python2 uses ValueError and perhaps others, python3 uses JSONDecodeError
    except Exception as e:
        app.logger.warn('Error parsing MQTT packet\n' + str(e))
        return

    try:
        if 'port' in msg:
            process_data(app, msg, payload_raw)
    except Exception as e:
        app.logger.warn('Error processing MQTT packet\n' + str(e))
        raise
        return

def process_data(app, msg, payload_raw):
    if msg["port"] != 1 and msg["port"] != 2:
        app.logger.info("Ignoring message with unknown port %s", msg["port"])
        return
    app.logger.debug("Raw msg: %s", binascii.hexlify(payload_raw))
    battery_num = msg["port"] - 1
    battery = device_to_battery(app, msg["dev_id"], battery_num)
    status = decode_status(payload_raw)
    status['battery'] = battery
    app.logger.debug("Decoded status: %s", status)
    core.process_uplink(status)

def mqtt_thread(client):
    client.loop_forever()

def battery_to_device(app, battery):
	""" Look up a battery id and return a tuple with device id and battery
	    index (within that device)."""
	for device, batteries in app.config['DEVICES'].items():
	    for i, b in enumerate(batteries):
		    if b == battery:
			    return device, i
	return None, None

def device_to_battery(app, device, battery_num):
	""" Look up a battery id for the given device id and index."""
	return app.config['DEVICES'][device][battery_num]

def send_command(app, config):
    app.logger.debug("Sending command: %s", str(config))
    device, battery_num = battery_to_device(app, config['battery'])

    msg = {
	"port": 1 + battery_num,
	"confirmed": False,
	"payload_raw": base64.b64encode(encode_command(config)).decode('ascii'),
	"schedule": "replace",
    }
    topic = "{}/devices/{}/down".format(app.config['TTN_APP_ID'], device)
    payload = json.dumps(msg)
    app.mqtt.publish(topic, payload)
    app.logger.debug("Publishing to topic %s: %s", topic, payload)

def encode_command(msg):
    raw = bytearray(16)
    raw[0] = msg['manualTimeout'] >> 8;
    raw[1] = msg['manualTimeout'] & 0xff;
    raw[2] = msg['pump'][0]
    raw[3] = msg['pump'][1]
    raw[4] = msg['pump'][2]
    raw[5] = msg['pump'][3]
    raw[6] = msg['targetFlow'];
    raw[7] = msg['targetLevel'][0];
    raw[8] = msg['targetLevel'][1];
    raw[9] = msg['targetLevel'][2];
    raw[10] = msg['minLevel'][0];
    raw[11] = msg['minLevel'][1];
    raw[12] = msg['minLevel'][2];
    raw[13] = msg['maxLevel'][0];
    raw[14] = msg['maxLevel'][1];
    raw[15] = msg['maxLevel'][2];
    return raw

def decode_status(raw):
    status = {}
    status['panic'] = False
    status['manualTimeout'] = raw[0] << 8 | raw[1]
    if status['manualTimeout'] == 0xffff: # -1 means panic
        status['manualTimeout'] = 0
        status['panic'] = True
    status['pump'] = [raw[2], raw[3], raw[4], raw[5]]
    status['currentFlow'] = [raw[6], raw[7]]
    status['targetFlow'] = raw[8]
    status['currentLevel'] = [raw[9], raw[10], raw[11]]
    status['targetLevel'] = [raw[12], raw[13], raw[14]]
    status['minLevel'] = [raw[15], raw[16], raw[17]]
    status['maxLevel'] = [raw[18], raw[19], raw[20]]
    return status

def run(app):
    client = mqtt.Client(userdata={'app': app})
    client.on_connect = on_connect
    client.on_message = on_message
    client.on_disconnect = on_disconnect
    client.on_log = on_log

    client.username_pw_set(app.config['TTN_APP_ID'], app.config['TTN_ACCESS_KEY'])
    client.tls_set(app.config['TTN_CA_CERT_PATH'])

    host = app.config['TTN_HOST']
    port = app.config['TTN_PORT']
    app.logger.info('Connecting to %s on port %s', host, port)

    client.connect(host, port=port)
    app.mqtt = client

    threading.Thread(target=mqtt_thread, args=(client,), daemon=True).start()