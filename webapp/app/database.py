import sqlite3
from flask import g

def setup_db(app):
    def get_db():
        """Opens a new database connection if there is none yet for the
        current application context.
        """
        if not hasattr(g, 'sqlite_db'):
            g.sqlite_db = sqlite3.connect(app.config['DATABASE'])
            g.sqlite_db.row_factory = sqlite3.Row
        return g.sqlite_db
    app.get_db = get_db

    @app.teardown_appcontext
    def close_db(error):
        """Closes the database again at the end of the request."""
        if hasattr(g, 'sqlite_db'):
            g.sqlite_db.close()

    @app.cli.command('initdb')
    def initdb_command():
        """Initializes the database."""
        db = app.get_db()
        with app.open_resource('schema.sql', mode='r') as f:
            db.cursor().executescript(f.read())
        db.commit()
        print('Initialized the database.')

def config_message_to_row(msg):
    return {
      'manualTimeout': msg['manualTimeout'],
      'battery': msg['battery'],
      'pump0': msg['pump'][0],
      'pump1': msg['pump'][1],
      'pump2': msg['pump'][2],
      'pump3': msg['pump'][3],
      'targetFlow': msg['targetFlow'],
      'targetLevel1': msg['targetLevel'][0],
      'targetLevel2': msg['targetLevel'][1],
      'targetLevel3': msg['targetLevel'][2],
      'minLevel1': msg['minLevel'][0],
      'minLevel2': msg['minLevel'][1],
      'minLevel3': msg['minLevel'][2],
      'maxLevel1': msg['maxLevel'][0],
      'maxLevel2': msg['maxLevel'][1],
      'maxLevel3': msg['maxLevel'][2],
    }

def config_row_to_message(row):
    return {
      'manualTimeout': row['manualTimeout'],
      'battery': row['battery'],
      'pump': [row['pump'][0], row['pump'][1], row['pump'][2], row['pump'][3]],
      'targetFlow': row['targetFlow'],
      'targetLevel': [row['targetLevel0'], row['targetLevel1'], row['targetLevel2']],
      'minLevel': [row['minLevel0'], row['minLevel1'], row['minLevel2']],
      'maxLevel': [row['maxLevel0'], row['maxLevel1'], row['maxLevel2']],
    }

def status_message_to_row(msg):
    return {
      'manualTimeout': msg['manualTimeout'],
      'battery': msg['battery'],
      'panic': msg['panic'],
      'pump0': msg['pump'][0],
      'pump1': msg['pump'][1],
      'pump2': msg['pump'][2],
      'pump3': msg['pump'][3],
      'targetFlow': msg['targetFlow'],
      'flowIn': msg['currentFlow'][0],
      'flowOut': msg['currentFlow'][1],
      'currentLevel1': msg['currentLevel'][0],
      'currentLevel2': msg['currentLevel'][1],
      'currentLevel3': msg['currentLevel'][2],
      'targetLevel1': msg['targetLevel'][0],
      'targetLevel2': msg['targetLevel'][1],
      'targetLevel3': msg['targetLevel'][2],
      'minLevel1': msg['minLevel'][0],
      'minLevel2': msg['minLevel'][1],
      'minLevel3': msg['minLevel'][2],
      'maxLevel1': msg['maxLevel'][0],
      'maxLevel2': msg['maxLevel'][1],
      'maxLevel3': msg['maxLevel'][2],
    }


def insert_from_dict(db, table, values):
    """
    Create and execute an insert query using the keys from the passed dict as
    field names and the values as the field values.  No escaping or checking
    happens on the field and table names.
    """
    query = 'insert into {}({}) values ({})'.format(
        table,
        ', '.join(values.keys()),
        ', '.join(['?'] * len(values)),
    )
    return db.execute(query, list(values.values()))


