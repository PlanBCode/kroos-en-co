import paho.mqtt.client as mqtt
import threading
import json
import base64
import binascii
import configparser

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
    calibrate_status(app, battery, status)
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
    device, battery_num = battery_to_device(app, config['battery'])

    calibrate_config(app, config['battery'], config)
    app.logger.debug("Sending command: %s", str(config))

    msg = {
	"port": 1 + battery_num,
	"confirmed": False,
	"payload_raw": base64.b64encode(encode_command(config)).decode('ascii'),
	"schedule": "replace",
    }
    topic = "{}/devices/{}/down".format(app.config['TTN_APP_ID'], device)
    payload = json.dumps(msg)
    if not app.config.TTN_RECEIVE_ONLY:
        app.mqtt.publish(topic, payload)
        app.logger.debug("Publishing to topic %s: %s", topic, payload)
    else:
        app.logger.debug("Would have published to topic %s: %s", topic, payload)

def encode_command(msg):
    raw = bytearray(16)
    raw[0] = msg['manualTimeout'] >> 8;
    raw[1] = msg['manualTimeout'] & 0xff;
    raw[2] = msg['pump'][0]
    raw[3] = msg['pump'][1]
    raw[4] = msg['pump'][2]
    raw[5] = msg['pump'][3]
    raw[6] = msg['targetFlow'];
    raw[7] = msg['targetLevelRaw'][0];
    raw[8] = msg['targetLevelRaw'][1];
    raw[9] = msg['targetLevelRaw'][2];
    raw[10] = msg['minLevelRaw'][0];
    raw[11] = msg['minLevelRaw'][1];
    raw[12] = msg['minLevelRaw'][2];
    raw[13] = msg['maxLevelRaw'][0];
    raw[14] = msg['maxLevelRaw'][1];
    raw[15] = msg['maxLevelRaw'][2];
    return raw

def calibrate_config(app, battery, config):
    for key in ('targetLevel', 'minLevel', 'maxLevel'):
        raw_key = key + 'Raw'

        config[raw_key] = []
        i = 1
        for cm in config[key]:
            to_mA = app.config['CALIBRATION_TO_MA']
            offset_mA = app.config['CALIBRATION_OFFSET_MA']
            ma_per_cm = app.calibration[battery].getfloat('factor_ma_per_cm_{}'.format(i))
            offset_cm = app.calibration[battery].getfloat('offset_cm_{}'.format(i))
            mA = (cm - offset_cm) * ma_per_cm
            raw = int((mA - offset_mA) / to_mA)
            # TODO: Error message for user?
            raw = min(255, max(0, raw))

            config[raw_key].append(raw)
            i += 1

def decode_status(raw):
    status = {}
    status['panic'] = False
    status['manualTimeout'] = (raw[0] << 8 | raw[1]) & 0x7FFF
    # MSB of the timeout field indicates panic
    if raw[0] & 0x80:
        status['panic'] = True
    status['pump'] = [raw[2], raw[3], raw[4], raw[5]]
    status['currentFlow'] = [raw[6], raw[7]]
    status['targetFlow'] = raw[8]
    status['currentLevelRaw'] = [raw[9], raw[10], raw[11]]
    status['targetLevelRaw'] = [raw[12], raw[13], raw[14]]
    status['minLevelRaw'] = [raw[15], raw[16], raw[17]]
    status['maxLevelRaw'] = [raw[18], raw[19], raw[20]]
    return status

def calibrate_status(app, battery, status):
    for key in ('currentLevel', 'targetLevel', 'minLevel', 'maxLevel'):
        raw_key = key + 'Raw'
        mA_key = key + 'mA'

        status[key] = []
        status[mA_key] = []
        i = 1

        for raw in status[raw_key]:
            to_mA = app.config['CALIBRATION_TO_MA']
            offset_mA = app.config['CALIBRATION_OFFSET_MA']
            ma_per_cm = app.calibration[battery].getfloat('factor_ma_per_cm_{}'.format(i))
            offset_cm = app.calibration[battery].getfloat('offset_cm_{}'.format(i))
            mA = raw * to_mA + offset_mA
            cm = mA / ma_per_cm + offset_cm

            status[mA_key].append(mA)
            status[key].append(cm)
            i += 1

CALIBRATION_FILE='calibration.ini'

def read_calibration(app):
    app.calibration = configparser.ConfigParser()
    app.calibration.read(CALIBRATION_FILE)

    for batteries in app.config['DEVICES'].values():
        for battery in batteries:
            for key, default_value in (
                ('factor_ma_per_cm_{}', app.config['DEFAULT_CALIBRATION_MA_PER_CM']),
                ('offset_cm_{}', app.config['DEFAULT_CALIBRATION_OFFSET_CM']),
            ):
                for i in range(1, 4):
                    indexed_key = key.format(i)
                    if battery not in app.calibration:
                        app.calibration[battery] = {}
                    if indexed_key not in app.calibration[battery]:
                        app.calibration[battery][indexed_key] = str(default_value)
    write_calibration(app)

def write_calibration(app):
    with open(CALIBRATION_FILE, 'w') as f:
        app.calibration.write(f)

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

    read_calibration(app)

    threading.Thread(target=mqtt_thread, args=(client,), daemon=True).start()

# vim: set sts=4 sw=4 expandtab:
