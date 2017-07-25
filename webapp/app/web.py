import jinja2
import flask
from . import core, app

@app.route('/')
def index():
    context = {
        'login': True,
        'id': 'lankheet-2',
    }
    try:
        return flask.render_template('index.html', **context)
    except jinja2.TemplateSyntaxError as e:
        print(e.lineno)

# vim: set sts=4 sw=4 expandtab:
