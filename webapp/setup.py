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
        'flask-user<0.7',
        'flask-mail',
    ],
)
