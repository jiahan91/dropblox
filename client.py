#!/usr/bin/env python
#
# This client connects to the centralized game server
# via http. After creating a new game on the game
# server, it spaws an AI subprocess called "dropblox_ai."
# For each turn, this client passes in the current game
# state to a new instance of dropblox_ai, waits ten seconds
# for a response, then kills the AI process and sends
# back the move list.
#

import contextlib
import hashlib
import httplib
import os
import platform
import sys
import threading
import time
import urllib2

import json

from subprocess import Popen, PIPE

from helpers import messaging
from helpers import urllib2_file

# Remote server to connect to:
PROD_HOST = 'playdropblox.com'
PROD_SSL = True # currently server requires this to be True
if PROD_SSL:
    PROD_PORT = 443
else:
    PROD_PORT = 80

# Subprocess
LEFT_CMD = 'left'
RIGHT_CMD = 'right'
UP_CMD = 'up'
DOWN_CMD = 'down'
ROTATE_CMD = 'rotate'
VALID_CMDS = [LEFT_CMD, RIGHT_CMD, UP_CMD, DOWN_CMD, ROTATE_CMD]

CLIENT_PATH = os.path.normpath(os.path.join(os.getcwd(), __file__))
AI_PROCESS_PATH = os.path.join(os.path.dirname(CLIENT_PATH), 'dropblox_ai')
NUM_HTTP_RETRIES = 2 # number of times to retry if http connection fails

is_windows = platform.system() == "Windows"

# Printing utilities
# TODO(astaley): Consider vetting and using colorama module for windows support

# default to no colors
colorred = '{0}'
colorgrn = colorred

try:
    import curses
    curses.setupterm()
    num_colors = curses.tigetnum('colors')
except Exception: # no term support (windows; piping to file; etc.)
    pass
else:
    if num_colors >= 8:
        colorred = "\033[01;31m{0}\033[00m"
        colorgrn = "\033[1;36m{0}\033[00m"

class Command(object):
    def __init__(self, cmd, *args):
        self.cmd = cmd
        self.args = list(args)

    def run(self, timeout):
        cmds = []
        process = Popen([self.cmd] + self.args, stdout=PIPE, universal_newlines=True, shell=is_windows)
        def target():
            for line in iter(process.stdout.readline, ''):
                line = line.rstrip('\n')
                if line not in VALID_CMDS:
                    print 'INVALID COMMAND:', line # Forward debug output to terminal
                else:
                    cmds.append(line)

        thread = threading.Thread(target=target)
        thread.start()

        thread.join(timeout)
        print colorred.format('Terminating process')
        try:
            process.terminate()
            thread.join(60)
        except Exception:
            pass
        print colorgrn.format('commands received: %s' % cmds)
        return cmds

class AuthException(Exception):
    pass

class GameOverError(Exception):
    def __init__(self, game_state_dict):
        self.game_state_dict = game_state_dict

class DropbloxServer(object):
    def __init__(self, team_name, team_password, host, port, ssl):
        # maybe support any transport
        # but whatever
        # TODO(astaley): Consider using persistent http connections to speed up client
        # Available in httplib or picloud's urllib2file
        self.host = host
        self.port = port
        self.ssl = ssl

        self.team_name = team_name
        self.team_password = team_password
        
    def _request(self, path, tbd):
        schema = 'https' if self.ssl else 'http'
        url = '%s://%s:%d%s' % (schema, self.host, self.port, path)
        
        tbd = dict(tbd)
        tbd['team_name'] = self.team_name
        tbd['password'] = self.team_password
        data = json.dumps(tbd)

        req = urllib2.Request(url, data, {
                'Content-Type': 'application/json'
                })
        
        for retry in range(NUM_HTTP_RETRIES+1): 
            try:
                with contextlib.closing(urllib2_file.urlopen(req)) as resp:
                    return json.loads(resp.read())
            
            except urllib2.HTTPError, err:
               if err.code == 401:
                   raise AuthException()
               if 500 <= err.code < 600:
                   if retry < NUM_HTTP_RETRIES:
                       print colorred.format('Received http error %s. Retrying...' % str(err))
                       time.sleep(0.5)
                       continue
                   else:
                       raise
               else:
                   raise
            except (urllib2.URLError, httplib.HTTPException), err:
                if retry < NUM_HTTP_RETRIES:
                   print colorred.format('Received %s error %s. Retrying...' % (type(err), str(err)))
                   time.sleep(0.5)
                   continue
                else:
                   raise

    def create_practice_game(self):
        return self._request("/create_practice_game", {})

    def get_compete_game(self):
        # return None if game is not ready to go yet
        resp = self._request("/get_compete_game", {})
        return resp

    def submit_game_move(self, game_id, move_list, moves_made):
        resp = self._request("/submit_game_move", {
                'game_id': game_id,
                'move_list': move_list,
                'moves_made': moves_made,
                })
        
        if resp['ret'] == 'ok':
            return resp

        elif resp['ret'] == 'fail':
            if resp['code'] == messaging.CODE_GAME_OVER:
                raise GameOverError(resp['game']['game_state'])
            elif resp['code'] == messaging.CODE_CONCURRENT_MOVE and \
              resp['game']['number_moves_made'] == moves_made + 1:
                # duplicate move; possible http error earlier - allow
                print colorred.format('Duplicate move sent; resolving')
                return resp
            else:
                raise Exception("Bad move: %r:%r",
                                resp['code'], resp['reason'])
        
        raise Exception("Bad response: %r" % (resp,))

def run_ai(game_state_dict, seconds_remaining):
    ai_arg_one = json.dumps(game_state_dict)
    ai_arg_two = json.dumps(seconds_remaining)
    command = Command(AI_PROCESS_PATH, ai_arg_one, ai_arg_two)
    ai_cmds = command.run(timeout=float(ai_arg_two))
    return ai_cmds

def run_game(server, game):
    game_id = game['game']['id']

    while True:
        moves_made = game['game']['number_moves_made']

        ai_cmds = run_ai(game['game']['game_state'],
                         game['competition_seconds_remaining'])

        try:
            game = server.submit_game_move(game_id, ai_cmds, moves_made)
        except GameOverError, e:
            final_game_state_dict = e.game_state_dict
            break

    print colorgrn.format("Game over! Your score was: %s" %
                          (final_game_state_dict['score'],))

def run_compete(server):
    # TODO: it might be better for this to be an actual game object
    #       instead of the dictionary serialization of it
    new_game = server.get_compete_game()

    # HAX: didn't have time to clean up this abstraction
    if new_game['ret'] == 'wait':
        wait_time = float(new_game.get('wait_time', 0.5))
        print colorred.format("Waiting to compete...")

    while new_game['ret'] == 'wait':
        time.sleep(wait_time)

        new_game = server.get_compete_game()
        # HAX: didn't have time to clean up this abstraction
        if new_game['ret'] == 'wait':
            wait_time = float(new_game.get('wait_time', 0.5))

    print colorred.format("Fired up and ready to go!")
    run_game(server, new_game)

def run_practice(server):
    # TODO: it might be better for this to be an actual game object
    #       instead of the dictionary serialization of it
    new_game = server.create_practice_game()
    run_game(server, new_game)

def main(argv):
    with open('config.txt', 'r') as f:
        team_name = f.readline().rstrip('\n')
        team_password = f.readline().rstrip('\n')

    if team_name == "TEAM_NAME_HERE" or team_password == "TEAM_PASSWORD_HERE":
        print colorred.format("Please specify a team name and password in config.txt")
        sys.exit(0)

    if (len(sys.argv) != 2 or
        sys.argv[1] not in ["compete", "practice"]):
        print colorred.format("Usage: client.py [compete|practice]")
        sys.exit(0)

    entry_mode = sys.argv[1]

    if os.environ.get('DROPBLOX_DEBUG'):
        connect_details = ('localhost', 8080, False)
    else:
        connect_details = (PROD_HOST, PROD_PORT, PROD_SSL)

    server = DropbloxServer(team_name, team_password, *connect_details)

    try:
        if entry_mode == "practice":
            run_practice(server)
            return 0
        elif entry_mode == "compete":
            run_compete(server)
            return 0
        else:
            assert False, 'wtf? mode = %s' % entry_mode
    except AuthException:
        print colorred.format("Cannot authenticate, please check config.txt")

if __name__ == '__main__':
    sys.exit(main(sys.argv))
