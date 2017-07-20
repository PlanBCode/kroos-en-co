Setup
-----
Create virtual python environment with the `venv` module in the
directory called `venv`, and activate it for the current shell:

	python -m venv venv
	source ./venv/bin/activate

Install the application into the python path, using `--editable` to
prevent copying files but instead add a link in the python path (so you
can edit the original files as normal).

	pip install --editable .

Populate database:

	FLASK_APP=app FLASK_DEBUG=1 flask initdb

Run server:

	FLASK_DEBUG=1 gunicorn --worker-class eventlet -w 1 app:app
