import flask_user
import wtforms
import click
import flask
from . import database, app

# Define the User data model. Make sure to add flask_user UserMixin !!!
db = database.sqla
class User(db.Model, flask_user.UserMixin):
    id = db.Column(db.Integer, primary_key=True)

    # User authentication information
    username = db.Column(db.String(50), nullable=False, unique=True)
    password = db.Column(db.String(255), nullable=False, server_default='')

    # User email information
    email = db.Column(db.String(255), nullable=False, unique=True)
    confirmed_at = db.Column(db.DateTime())

    # User information
    active = db.Column('is_active', db.Boolean(), nullable=False, server_default='0')
    first_name = db.Column(db.String(100), nullable=False, server_default='')
    last_name = db.Column(db.String(100), nullable=False, server_default='')

class UserInvitation(db.Model):
    __tablename__ = 'user_invite'
    id = db.Column(db.Integer, primary_key=True)
    email = db.Column(db.String(255), nullable=False)
    # save the user of the invitee
    invited_by_user_id = db.Column(db.Integer, db.ForeignKey('user.id'))
    # token used for registration page to identify user registering
    token = db.Column(db.String(100), nullable=False, server_default='')

def password_validator(form, field):
    if len(field.data) < 8:
        raise wtforms.validators.ValidationError('Password must have at least 8 characters')

# Setup Flask-User. Creating the UserManager will register all kinds of stuff,
# such as routes for login, register, etc.
db_adapter = flask_user.SQLAlchemyAdapter(database.sqla, User, UserInvitationClass=UserInvitation)
user_manager = flask_user.UserManager(
                app=app,
                db_adapter=db_adapter,
                password_validator=password_validator,
)

@app.cli.command('invite')
@click.argument('email')
def initdb_command(email):
    """Invite a user to this project."""
    user_invite = db_adapter.add_object(
                db_adapter.UserInvitationClass,
                    email = email,
                    invited_by_user_id = -1,
                )
    db_adapter.commit()
    user_invite.token = user_manager.generate_token(user_invite.id)
    db_adapter.commit()

    accept_invite_link = flask.url_for('user.register', token=user_invite.token, _external=True)

    try:
        # Send 'invite' email
        flask_user.emails.send_invite_email(user_invite, accept_invite_link)
    except Exception as e:
        # delete new User object if send fails
        db_adapter.delete_object(user_invite)
        db_adapter.commit()
        raise

    print('Sent invite to ' + email)

# vim: set sw=4 sts=4 expandtab:
