Setup
-----
Create virtual python environment with the `venv` module in the
directory called `venv`, and activate it for the current shell. This
needs python3.5 or newer, so use `python3.5` or higher instead of
`python3` if the latter is not new enough:

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

	FLASK_APP=app flask initdb

To create an initial user, send out an invitation using:

	FLASK_SERVER_NAME=localhost:8000 FLASK_APP=app flask invite info@example.org

`SERVER_NAME` is used to generate the activation link so might need to be
changed. Of course also change the e-mail address passed to your own.

Run server locally:

	FLASK_DEBUG=1 gunicorn --worker-class eventlet -w 1 app:app

Or, to expose to the outside world, add a -b option:

	FLASK_DEBUG=1 gunicorn --worker-class eventlet -w 1 app:app -b 0.0.0.0:8000
