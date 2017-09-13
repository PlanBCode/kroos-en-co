from setuptools import setup

setup(
    name='Pump controller & dashboard',
    packages=['app'],
    include_package_data=True,
    install_requires=[
        'flask',
	'flask-socketio',
	'eventlet',
	'paho-mqtt',
        'gunicorn',
	'PyMySQL',
	# 0.6.14 broke installs, 0.6.19 fixed them
	# https://github.com/lingthio/Flask-User/issues/183
        'flask-user<0.7,!=0.6.14,!=0.6.15,!=0.6.16,!=0.6.17,!=0.6.18'
        'flask-mail',
    ],
)
