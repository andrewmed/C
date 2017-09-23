#!/usr/bin/env python3

import sys
import os
import json
import shutil
import datetime
import stat
import tty
import typing

import termios

BUFFER_NAME = '~/.c_copy_buffer'

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

CHOICES = [
    'y',
    'n',
    'Y',
    'N',
]

class COLORS:
    N = ''
    HEADER = '\033[95m'
    OKBLUE = '\033[94m'
    OKGREEN = '\033[92m'
    WARNING = '\033[93m'
    FAIL = '\033[91m'
    ENDC = '\033[0m'
    BOLD = '\033[1m'
    UNDERLINE = '\033[4m'


def output_coll(coll, color = COLORS.N):
    if coll:
        output = '\n'.join(sorted(list(coll)))
        sys.stdout.write(color + output + COLORS.ENDC + '\n')
        sys.stdout.flush()

def output_text(text, color = COLORS.N):
    sys.stdout.write(color + text + COLORS.ENDC)
    sys.stdout.flush()


class Storage:
    def __init__(self, path=os.path.expanduser(BUFFER_NAME)):
        self.path = path
        self.files = set()
        self.dirs = set()

    def load(self):
        if os.path.isfile(self.path):
            with open(self.path) as f:
                input = json.load(f)
                self.files = set(input['files'])
                self.dirs = set(input['dirs'])

    def persist(self):
        data = {'files' : list(self.files), 'dirs' : list(self.dirs)}
        with open(self.path, 'w') as f:
            json.dump(data, f)

    def clean(self):
        self.files = set()
        self.dirs = set()

    def get_info(self) -> str:
        files = len(self.files)
        dirs = len(self.dirs)
        if not files and not dirs:
            return ''
        if files and dirs:
            message = '%s files and %s dirs' % (files, dirs)
        else:
            message = '%s files' % files if files else '%s dirs' % dirs
        return message


class CmdBase:
    AUTOLOAD = False
    NEEDFILES = False
    ACCEPTPARAMS = False

    PRINTINFO = True

    def __init__(self, args):
        self.args = args
        if not self.ACCEPTPARAMS and self.args:
            raise Exception('This method does not accept arguments')
        self.storage = Storage()
        if self.AUTOLOAD:
            self.storage.load()
        if self.NEEDFILES and not len(self.storage.files) and not len(self.storage.dirs):
            raise Exception('Please select some files with `+c` first')

    def do(self):
        raise Exception('Not implemented')

    def print_info(self):
        info = self.storage.get_info()
        if info:
            output_text(info + ' total\n', COLORS.BOLD)

    def doit(self):
        self.do()
        if self.PRINTINFO:
            self.print_info()


class c(CmdBase):
    AUTOLOAD = True
    ACCEPTPARAMS = True

    def do(self):
        if not self.args:
            self.storage.dirs.add(os.getcwd())
        for arg in self.args:
            file = os.path.realpath(arg)
            if not os.path.exists(file):
                raise Exception(file + ' does not exist')
            if os.path.isfile(file):
                self.storage.files.add(file)
                output_text('%s  ' % os.path.basename(arg))
                continue
            if os.path.isdir(file):
                self.storage.dirs.add(file)
                output_text('%s  ' % os.path.basename(arg), COLORS.OKBLUE)
                continue
            raise Exception(file + ' has unknown file type')
        print()
        self.storage.persist()


class C(c):
    AUTOLOAD = False


class v(CmdBase):
    AUTOLOAD = True
    NEEDFILES = True
    ACCEPTPARAMS = True
    PRINTINFO = False

    overwrite_mode = 'y'

    def overwrite(self, source):
        #fixme
        if self.args and not os.path.exists(self.args[0]):
            os.mkdir(self.args[0])
        to_dir = os.path.abspath(self.args[0]) if self.args else os.getcwd()
        dst = os.path.join(to_dir, os.path.basename(source))
        if os.path.exists(dst):
            if not self.overwrite_mode in {'Y', 'N'}:
                print('from:\t %s %s %s' % (datetime.datetime.fromtimestamp(os.path.getmtime(source)), os.path.getsize(source), source))
                print('to:\t %s %s %s' % (datetime.datetime.fromtimestamp(os.stat(dst).st_mtime), os.path.getsize(dst), dst))
                output_text('Overwrite? (%s) ' % '/'.join(CHOICES), COLORS.WARNING)
                try:
                    fd = sys.stdin.fileno()
                    old = termios.tcgetattr(fd)
                    tty.setraw(fd)
                    while True:
                        char = sys.stdin.read(1)
                        if ord(char) in {3, 4}:
                            raise Exception('Aborted')
                        if char in CHOICES:
                            output_text(char, COLORS.WARNING)
                            break
                    self.overwrite_mode = char
                finally:
                    termios.tcsetattr(fd, termios.TCSADRAIN, old)
                    print()
        return dst, self.overwrite_mode in {'y', 'Y'}

    def do(self):
        files_copy = self.storage.files.copy()
        dirs_copy = self.storage.dirs.copy()
        skipped = 0

        if len(self.args) > 1:
            raise Exception('only one parameter allowed')
        try:
            for src in dirs_copy:
                dst, write = self.overwrite(src)
                if not write:
                    skipped += 1
                    continue
                #fixme overwrite
                shutil.copytree(src, dst, symlinks=True)
                self.storage.dirs.remove(src)

            for src in files_copy:
                dst, write = self.overwrite(src)
                if not write:
                    skipped += 1
                    continue
                shutil.copy2(src, dst)
                self.storage.files.remove(src)
        finally:
            color = COLORS.FAIL if skipped else COLORS.HEADER
            output_text('%s/%s files, %s/%s dirs copied, %s skipped\n' %
                        (len(files_copy) - len(self.storage.files), len(files_copy),
                         len(dirs_copy) - len(self.storage.dirs), len(dirs_copy), skipped), color)


class V(v):
    def do(self):
        super().do()
        self.storage.persist()


class R(CmdBase):
    AUTOLOAD = True

    def do(self):
        info = self.storage.get_info()
        self.storage.clean()
        self.storage.persist()
        if info:
            print(info + ' from buffer cleared')


class DEL(CmdBase):
    AUTOLOAD = True
    NEEDFILES = True

    def do(self):
        files_copy = self.storage.files.copy()
        dirs_copy = self.storage.dirs.copy()
        try:
            for source in files_copy:
                os.remove(source)
                self.storage.files.remove(source)
            for source in dirs_copy:
                shutil.rmtree(source)
                self.storage.dirs.remove(source)
        finally:
            output_text('%s files %s dirs deleted\n' % (len(files_copy) - len(self.storage.files), len(dirs_copy) - len(self.storage.dirs)), COLORS.HEADER)
        self.storage.persist()

class h(CmdBase):
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

class p(CmdBase):
    AUTOLOAD = True

    def do(self):
        output_coll(self.storage.dirs, COLORS.OKBLUE)
        output_coll(self.storage.files)

def main(argv):
    callname = os.path.basename(argv[0])
    if callname not in COMMANDS:
        raise Exception('only following commands are accepted: %s' % ' '.join(COMMANDS))
    cmd = eval(callname[1:])(argv[1:])
    cmd.doit()

if __name__ == '__main__':
    try:
        main(sys.argv)
    except Exception as e:
        output_text(str(e) + '\n', COLORS.FAIL)
        exit(-1)

