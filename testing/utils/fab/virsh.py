# Stuff to talk to virsh, for libreswan
#
# Copyright (C) 2015-2016 Andrew Cagney <cagney@gnu.org>
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; either version 2 of the License, or (at your
# option) any later version.  See <https://www.gnu.org/licenses/gpl2.txt>.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
# or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
# for more details.

import subprocess
import pexpect
import sys
import time
import logging
import re
import os

from fab import console
from fab import logutil
from fab import timing

# Can be anything as it either matches immediately or dies with EOF.
CONSOLE_TIMEOUT = 30

class STATE:
    RUNNING = "running"
    IDLE = "idle"
    PAUSED = "paused"
    SHUTDOWN = "shutdown"
    SHUT_OFF = "shut off"
    CRASHED = "crashed"
    DYING = "dying"
    PMSUSPENDED = "pmsuspended"

_VIRSH = ["sudo", "virsh", "--connect", "qemu:///system"]

SHUTDOWN_TIMEOUT = 20
DESTROY_TIMEOUT = 20

class Domain:

    def __init__(self, logger, host_name=None, domain_name=None):
        # Use the term "domain" just like virsh
        self.domain_name = domain_name
        self.host_name = host_name
        self.virsh_console = None
        self.logger = logger
        self.debug_handler = None
        self.logger.debug("domain created")
        self._mounts = None
        self._xml = None
        self._console = None # or False or a real console

    def __str__(self):
        return "domain " + self.domain_name

    def nest(self, logger, prefix):
        self.logger = logger.nest(prefix + self.domain_name)
        if self._console:
            self._console.logger = self.logger
        return self.logger

    def run_status_output(self, command):
        self.logger.debug("running: %s", command)
        # 3.5 has subprocess.run
        process = subprocess.Popen(command,
                                   stdin=subprocess.DEVNULL,
                                   stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT)
        stdout, stderr = process.communicate()
        status = process.returncode
        output = stdout.decode('utf-8').strip()
        if status:
            self.logger.debug("virsh exited with unexpected status code %s\n%s",
                              status, output)
        else:
            self.logger.debug("output: %s", output)
        return status, output

    def state(self):
        status, output = self.run_status_output(_VIRSH + ["domstate", self.domain_name])
        if status:
            return None
        else:
            return output

    def _shutdown(self):
        self.run_status_output(_VIRSH + ["shutdown", self.domain_name])

    def shutdown(self, timeout=SHUTDOWN_TIMEOUT):
        """Use the console to detect the shutdown - if/when the domain stops
        it will exit giving an EOF.

        """

        console = self.console()
        if not console:
            self.logger.error("domain already shutdown")
            return True

        self.logger.info("waiting %d seconds for domain to shutdown", timeout)
        lapsed_time = timing.Lapsed()
        self._shutdown()
        if console.expect([pexpect.EOF, pexpect.TIMEOUT],
                          timeout=timeout) == 0:
            self.logger.info("domain shutdown after %s", lapsed_time)
            self._console = False
            self.logger.info("domain state is: %s", self.state())
            return True

        self.logger.error("timeout shutting down domain")
        return self.destroy()

    def _destroy(self):
        return self.run_status_output(_VIRSH + ["destroy", self.domain_name])

    def destroy(self, timeout=DESTROY_TIMEOUT):
        """Use the console to detect a destroyed domain - if/when the domain
        stops it will exit giving an EOF.

        """

        console = self.console()
        if not console:
            self.logger.error("domain already destroyed")
            return True

        self.logger.info("waiting %d seconds for domain to be destroyed", timeout)
        lapsed_time = timing.Lapsed()
        self._destroy()
        if console.expect([pexpect.EOF, pexpect.TIMEOUT],
                          timeout=timeout) == 0:
            self.logger.info("domain destroyed after %s", lapsed_time)
            self._console = False
            return True

        self.logger.error("timeout destroying domain, giving up")
        self._console = None
        return False

    def reboot(self):
        return self.run_status_output(_VIRSH + ["reboot", self.domain_name])

    def start(self):
        self._console = None
        return self.run_status_output(_VIRSH + ["start", self.domain_name])

    def dumpxml(self):
        if self._xml == None:
            status, self._xml = self.run_status_output(_VIRSH + ["dumpxml", self.domain_name])
            if status:
                raise AssertionError("dumpxml failed: %s" % (output))
        return self._xml

    def console(self, timeout=CONSOLE_TIMEOUT):
        if self._console:
            self.logger.info("console already open")
            return self._console
        if self._console is False:
            self.logger.info("console already failed")
            return self._console

        # self._console is None
        command_args = _VIRSH + ["console", "--force", self.domain_name]
        self.logger.info("spawning: %s", " ".join(command_args))
        self._console = console.Remote(command_args, self.logger, hostname=self.host_name)
        # Give the virsh process a chance set up its control-c
        # handler.  Otherwise something like control-c as the first
        # character sent might kill it.  If the machine is down, it
        # will get an EOF.
        if self._console.expect([pexpect.EOF,
                                 # earlier
                                 "Connected to domain %s\r\nEscape character is \\^]" % self.domain_name,
                                 # libvirt >= 7.0
                                 "Connected to domain '%s'\r\nEscape character is \\^] \(Ctrl \+ ]\)\r\n" % self.domain_name
                                 ],
                                timeout=timeout) > 0:
            return self._console

        self.logger.debug("got EOF from console")
        self._console.close()
        self._console = False

    def close(self):
        if self._console:
            self._console.close()
        self._console = None # not False

    def _get_mounts(self, console):
        # First extract the 9p mount points from LIBVIRT.
        #
        # The code works kind of but not really like a state machine.
        # Specific lines trigger actions.
        mount_points = {}
        for line in self.dumpxml().splitlines():
            if re.compile("<filesystem type='mount' ").search(line):
                source = ""
                target = ""
                continue
            match = re.compile("<source dir='([^']*)'").search(line)
            if match:
                source = match.group(1)
                # Strip trailing "/" along with other potential quirks
                # such as the mount point being a soft link.
                source = os.path.realpath(source)
                continue
            match = re.compile("<target dir='([^']*)'").search(line)
            if match:
                target = match.group(1)
                continue
            if re.compile("<\/filesystem>").search(line):
                self.logger.debug("filesystem '%s' '%s'", target, source)
                mount_points[target] = source
                continue
        # now query the domain for its fstab, save it in regex
        # group(1); danger binary data!
        console.sendline("cat /etc/fstab")
        status, output = console.expect_prompt(rb'(.*)')
        if status:
            raise AssertionError("extracting fstab failed: %s", status)
        fstab = output.group(1).decode('utf-8')
        # convert the fstab into a second map; look for NFS and 9p
        # mounts
        mounts = []
        for line in fstab.splitlines():
            self.logger.debug("line: %s", line)
            if line.startswith("#"):
                continue
            fields = line.split()
            if len(fields) < 3:
                continue
            device = fields[0]
            mount = fields[1]
            fstype = fields[2]
            if fstype == "nfs":
                self.logger.debug("NFS mount '%s''%s'", device, mount)
                device = device.split(":")[1]
                # switch to utf-8
                mounts.append((device, mount))
            elif fstype == "9p":
                if device in mount_points:
                    self.logger.debug("9p mount '%s' '%s'", device, mount)
                    device = mount_points[device]
                    mounts.append((device, mount))
                else:
                    self.logger.info("9p mount '%s' '%s' is not in domain description", device, mount)
            else:
                self.logger.debug("skipping %s mount '%s' '%s'", fstype, device, mount)

        mounts = sorted(mounts, key=lambda item: item[0], reverse=True)
        return mounts

    def guest_path(self, host_path):
        console = self.console()
        if self._mounts is None:
            self._mounts = self._get_mounts(console)
        # Note that REMOTE_MOUNTS is sorted in reverse order so that
        # /source/testing comes before /source.  This way the logger
        # path is matched first.
        self.logger.debug("ordered mounts %s", self._mounts);
        host_path = os.path.realpath(host_path)
        for host_directory, guest_directory in self._mounts:
            self.logger.debug("host %s guest %s path %s", host_directory, guest_directory, host_path)
            if os.path.commonprefix([host_directory, host_path]) == host_directory:
                # Found the local directory containing path that is
                # mounted on the guest; now convert that into an
                # absolute guest path.
                return guest_directory + host_path[len(host_directory):]

        raise AssertionError("the host path '%s' is not mounted on the guest %s" % (host_path, self))
