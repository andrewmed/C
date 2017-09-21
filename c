import sys
import os
import json
import itertools
import shutil
import platform
import datetime
import stat
import tty

import termios

BUFFER_NAME = os.path.expanduser('~/.c_copy_buffer')
DISPLAY_THRESHOLD = 10

COMMANDS = [
    '+c',
    '+C',
    '+v',
    '+V',
    '+R',
    '+h',
    '+p',
    '+DEL'
]

OVERWRITE = [
    'y',
    'n',
    'Y',
    'N',
]

class COLORS:
    HEADER = '\033[95m'
    OKBLUE = '\033[94m'
    OKGREEN = '\033[92m'
    WARNING = '\033[93m'
    FAIL = '\033[91m'
    ENDC = '\033[0m'
    BOLD = '\033[1m'
    UNDERLINE = '\033[4m'




def printColl(coll, color = COLORS.ENDC):
    if not coll:
        return
    output = sorted(list(coll))
    print(color, end='')
    # print(*output, sep='\n')
    print('\n'.join(output))
    print(COLORS.ENDC, end='')

def colorText(text, color = COLORS.ENDC):
    print('%s%s%s' % (color, text, COLORS.ENDC))



class cmd_base:
    autoload = True
    params_allowed = False
    buffer_needed = False

    files = set()
    dirs = set()
    
    def __init__(self, args):
        self.args = args
        if not self.params_allowed and self.args:
            raise Exception('This method does not accept arguments')
        if self.autoload and os.path.isfile(BUFFER_NAME):
            with open(BUFFER_NAME) as f:
                input = json.load(f)
                self.files = set(input['files'])
                self.dirs = set(input['dirs'])
        total_size = len(self.files) + len(self.dirs)
        if self.buffer_needed and not total_size:
            raise Exception('Please select some files with `+c` first')

    def do(self):
        raise Exception('Not implemented')

    def persist(self):
        data = {'files' : list(self.files), 'dirs' : list(self.dirs)}
        with open(BUFFER_NAME, 'w') as f:
            json.dump(data, f)

    def print_info(self):
        colorText ('%s files and %s dirs ' % (len(self.files), len(self.dirs)), COLORS.BOLD)



class c(cmd_base):
    params_allowed = True
    def do(self):
        if not self.args:
            self.dirs.add(os.getcwd())
        for arg in self.args:
            file = os.path.realpath(arg)
            if not os.path.exists(file):
                raise Exception(file + ' does not exist')
            if os.path.isfile(file):
                self.files.add(file)
                continue
            if os.path.isdir(file):
                self.dirs.add(file)
                continue
            raise Exception(file + ' has unknown file type')
        self.persist()
        self.print_info()


class C(c):
    autoload = False


class v(cmd_base):
    params_allowed = True
    buffer_needed = True
    overwrite_mode = 'y'

    def overwrite(self, source):
        if self.args:
            to_dir = os.path.abspath(self.args[0])
        else:
            to_dir = os.getcwd()
        dst = os.path.join(to_dir, os.path.basename(source))
        if os.path.exists(dst):
            if not self.overwrite_mode in {'Y', 'N'}:
                print('from:\t %s %s %s %s' % (datetime.datetime.fromtimestamp(os.path.getmtime(source)), os.path.getsize(source), stat.S_IMODE(os.lstat(dst).st_mode),source))
                print('to:\t %s %s %s %s' % (datetime.datetime.fromtimestamp(os.stat(dst).st_mtime), os.path.getsize(dst), stat.S_IMODE(os.lstat(dst).st_mode), dst))
                colorText('Overwrite? (%s)' % '/'.join(OVERWRITE), COLORS.WARNING)
                try:
                    fd = sys.stdin.fileno()
                    old = termios.tcgetattr(fd)
                    tty.setraw(fd)
                    while True:
                        char = sys.stdin.read(1)
                        if ord(char) == 3:
                            raise Exception('Aborted')
                        if char in OVERWRITE:
                            break
                    self.overwrite_mode = char
                finally:
                    termios.tcsetattr(fd, termios.TCSADRAIN, old)
        return dst, self.overwrite_mode in {'y', 'Y'}

    def do(self):
        file_coll = self.files.copy()
        dirs_coll = self.dirs.copy()
        skipped = 0

        if len(self.args) > 1:
            raise Exception('only one parameter allowed')

        try:
            for src in dirs_coll:
                dst, write = self.overwrite(src)
                if not write:
                    skipped += 1
                    continue
                #fixme overwrite
                # suffix = '.bak.bak.bak'
                shutil.copytree(src, dst, symlinks=True)
                self.dirs.remove(src)
                # shutil.rmtree(dst)

            for src in file_coll:
                dst, write = self.overwrite(src)
                if not write:
                    skipped += 1
                    continue
                shutil.copy2(src, dst)
                self.files.remove(src)

        finally:
            colorText('%s files %s dirs copied, %s skipped' % (len(file_coll) - len(self.files), len(dirs_coll) - len(self.dirs), skipped), COLORS.BOLD)


class V(v):
    def do(self):
        super().do()
        self.persist()


class R(cmd_base):
    autoload = False
    def do(self):
        if os.path.isfile(BUFFER_NAME):
            self.print_info()
            os.remove(BUFFER_NAME)


class DEL(cmd_base):
    def do(self):
        file_coll = self.files.copy()
        dirs_coll = self.dirs.copy()
        try:
            for source in file_coll:
                os.remove(source)
                self.files.remove(source)

            for source in dirs_coll:
                shutil.rmtree(source)
                self.dirs.remove(source)
        finally:
            colorText('%s files %s dirs deleted' % (len(file_coll) - len(self.files), len(dirs_coll) - len(self.dirs)), COLORS.BOLD)
            self.persist()


class h(cmd_base):
    autoload = False
    def do(self):
        print(
'''
Usage:
+h  this help message
+c  add files to buffer
+C  add files to buffer (buffer is cleared first)
+v  copy files to current directory
+V  copy files to current directory AND clear buffer
+p  print buffer content
+R  just clear the buffer
+DEL delete files and clear buffer
'''
        )

class p(cmd_base):
    def do(self):
        printColl(self.dirs, color = COLORS.OKBLUE)
        printColl(self.files)
        self.print_info()

def main(argv):
    callname = os.path.basename(argv[0])
    if callname not in COMMANDS:
        raise Exception('only following commands accepted: %s' % ' '.join(COMMANDS))
    cmd = eval(callname[1:])(argv[1:])
    cmd.do()

if __name__ == '__main__':
    try:
        if __name__ == '__main__':
            main(sys.argv)
    except Exception as e:
        sys.stderr.write( '%s%s%s%s' % (COLORS.FAIL, str(e), COLORS.ENDC, '\n'))
        exit(-1)

