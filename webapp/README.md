Setup
-----
Create virtual python environment with the `venv` module in the
directory called `venv`, and activate it for the current shell:

	python3 -m venv venv
	source ./venv/bin/activate

(alternatively, you can call commands inside the venv manually without
activating it first, e.g. ./venv/bin/pip)

Install the application into the python path, using `--editable` to
prevent copying files but instead add a link in the python path (so you
can edit the original files as normal).

	pip install --editable .

This might show some errors like `Failed building wheel for foo`, which is not
a problem, since it later retries these packages using another build method and
should show `Running setup.py install for foo ... done`

Create config:

	cp config.py.tmpl config.py

Change `config.py` with the needed values, then populate the database:

	FLASK_APP=app FLASK_DEBUG=1 flask initdb

Run server locally:

	FLASK_DEBUG=1 gunicorn --worker-class eventlet -w 1 app:app

Or, to expose to the outside world, add a -b option:

	FLASK_DEBUG=1 gunicorn --worker-class eventlet -w 1 app:app -b 0.0.0.0:8000
