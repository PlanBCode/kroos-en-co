from flask import g
from flask_sqlalchemy import SQLAlchemy
from datetime import datetime

from . import app

sqla = SQLAlchemy(app)

def get_sqlite_db():
    """Opens a new database connection if there is none yet for the
    current application context.
    """
    import sqlite3
    if not hasattr(g, 'sqlite_db'):
        g.sqlite_db = sqlite3.connect(app.config['DATABASE'])
        g.sqlite_db.row_factory = sqlite3.Row
    return g.sqlite_db

def get_mysql_db():
    """Opens a new database connection if there is none yet for the
    current application context.
    """
    import pymysql

    if not hasattr(g, 'mysql_db'):
        g.mysql_db = pymysql.connect(
            host=app.config['MYSQL_HOST'],
            user=app.config['MYSQL_USERNAME'],
            password=app.config['MYSQL_PASSWORD'],
            db=app.config['MYSQL_DB'],
            charset='utf8',
            cursorclass=pymysql.cursors.DictCursor)
    return g.mysql_db

if 'MYSQL_DB' in app.config:
        app.get_db = get_mysql_db
        placeholder = '%s'
        datetime_fmt = '%Y-%m-%d %H:%M:%S'
else:
        app.get_db = get_sqlite_db
        placeholder = '?'
        datetime_fmt = '%Y-%m-%d %H:%M:%S.%f'

@app.teardown_appcontext
def close_db(error):
    """Closes the database again at the end of the request."""
    if hasattr(g, 'sqlite_db'):
        g.sqlite_db.close()

@app.cli.command('initdb')
def initdb_command():
    """Initializes the database."""
    db = app.get_db()
    c = db.cursor()
    with app.open_resource('schema.sql', mode='r') as f:
        # sqlite3 needs executescript to run multiple statements, Mysql
        # can do it in a normal execute
        if hasattr(c, 'executescript'):
            c.executescript(f.read())
        else:
            c.execute(f.read())
    db.commit()
    sqla.create_all();
    print('Initialized the database.')


def parse_timestamp(timestamp):
    if timestamp is None or isinstance(timestamp, datetime):
        return timestamp
    return datetime.strptime(timestamp, datetime_fmt)


def config_message_to_row(msg):
    return {
      'ackTimestamp': msg['ackTimestamp'],
      'username': msg['username'],
      'timestamp': msg['timestamp'],
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
      'id': row['id'],
      'username': row['username'],
      'timestamp': parse_timestamp(row['timestamp']),
      'ackTimestamp': parse_timestamp(row['ackTimestamp']),
      'manualTimeout': row['manualTimeout'],
      'battery': row['battery'],
      'pump': [row['pump0'], row['pump1'], row['pump2'], row['pump3']],
      'targetFlow': row['targetFlow'],
      'targetLevel': [row['targetLevel1'], row['targetLevel2'], row['targetLevel3']],
      'minLevel': [row['minLevel1'], row['minLevel2'], row['minLevel3']],
      'maxLevel': [row['maxLevel1'], row['maxLevel2'], row['maxLevel3']],
    }

def status_message_to_row(msg):
    # TODO: Store currentLevelRaw values?
    return {
      'timestamp': msg['timestamp'],
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

def status_row_to_message(row):
    return {
      'id': row['id'],
      'timestamp': parse_timestamp(row['timestamp']),
      'manualTimeout': row['manualTimeout'],
      'battery': row['battery'],
      'pump': [row['pump0'], row['pump1'], row['pump2'], row['pump3']],
      'targetFlow': row['targetFlow'],
      'targetLevel': [row['targetLevel1'], row['targetLevel2'], row['targetLevel3']],
      'minLevel': [row['minLevel1'], row['minLevel2'], row['minLevel3']],
      'maxLevel': [row['maxLevel1'], row['maxLevel2'], row['maxLevel3']],
      'currentFlow': [row['flowIn'], row['flowOut']],
      'currentLevel': [row['currentLevel1'], row['currentLevel2'], row['currentLevel3']],
      'panic': row['panic'],
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
        ', '.join([placeholder] * len(values)),
    )
    c = db.cursor()
    c.execute(query, list(values.values()))
    db.commit()
    return c

def update_from_dict(db, table, where, values):
    """
    Create and execute an insert query using the keys from the passed dict as
    field names and the values as the field values.  No escaping or checking
    happens on the field and table names.
    """
    query = 'update {} set {} where {}'.format(
        table,
        ', '.join('{}={}'.format(f, placeholder) for f in values.keys()),
        ' and '.join('{}={}'.format(f, placeholder) for f in where.keys()),
    )
    c = db.cursor()
    c.execute(query, list(values.values()) + list(where.values()))
    db.commit()
    return c

def get_most_recent(db, table, values):
    """
    Get the most recent entry from the passed table, based on the timestamp field.
    """
    where = ''
    if values:
        where = 'where ' + ' and '.join('{}={}'.format(f, placeholder) for f in values.keys())
    query = 'select * from {} {} order by timestamp desc limit 1'.format(table, where)
    c = db.cursor()
    c.execute(query, list(values.values()))
    return c.fetchone()

# vim: set sw=4 sts=4 expandtab:
