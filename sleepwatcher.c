/*
 *    sleepwatcher
 *
 *    Copyright (c) 2002-2011 Bernhard Baehr
 *
 *    sleepwatcher.c - sleep mode watchdog program
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *    You should have received a copy of the GNU General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <libgen.h>

#include <CoreFoundation/CoreFoundation.h>
#include <CoreServices/CoreServices.h>

#include <IOKit/IOKitLib.h>
#include <IOKit/IOMessage.h> // kIOMessageSystemWillSleep, kIOMessageSystemWillNotSleep
#include <IOKit/pwr_mgt/IOPMLib.h>
#include <IOKit/ps/IOPowerSources.h>


static struct args {
    int argc;
    char* const *argv;
    char* progname;
    char* sleepcommand;
} args;


void message(int priority, const char *msg, ...) {
    va_list ap;
    FILE *out;

    if (priority < LOG_INFO) {
        va_start(ap, msg);
        out = (priority == LOG_INFO) ? stdout : stderr;
        fprintf(out, "%s: ", args.progname);
        vfprintf(out, msg, ap);
        fflush(out);
    }
}

void writePidFile(char *pidfile) {
    FILE *fp;
    if (pidfile) {
        fp = fopen(pidfile, "w");
        if (fp) {
            fprintf(fp, "%d", getpid());
            fclose(fp);
        }
        else {
            message(LOG_ERR, "can't write pidfile %s\n", pidfile);
        }
    }
}

static void usage(void) {
    printf(
        "Usage: %s [-s sleepcommand]\n"
        "Daemon to monitor sleep of your Mac\n"
        "-s or --sleep\n"
        "       execute sleepcommand when the Mac is put to sleep mode\n"
        "       (sleepcommand must not take longer than 15 seconds because\n"
        "       after this timeout the sleep mode is forced by the system)\n",
        args.progname
    );
    exit(2);
}

static void parseArgs(int argc, char * const *argv) {
    args.argc = argc;
    args.argv = argv;
    args.progname = basename(*argv);
    args.sleepcommand = NULL;
    writePidFile(NULL);
    if (argc == 2) {
        args.sleepcommand = argv[1];
    }
    else {
        usage();
    }
}

void powerCallback(void *rootPort, io_service_t y, natural_t msgType, void *msgArgument) {
    /*
        fprintf (stderr, "powerCallback: message_type %08lx, arg %08lx\n",
            (long unsigned int) msgType, (long  unsigned int) msgArgument);
     */
    switch (msgType) {
        case kIOMessageSystemWillSleep:
            if (args.sleepcommand) {
                message(
                    LOG_INFO,
                    "sleep: %s: %d\n",
                    args.sleepcommand,
                    system(args.sleepcommand)
                );
            }
            IOAllowPowerChange(*(io_connect_t *) rootPort, (long) msgArgument);
            break;
        case kIOMessageSystemWillNotSleep:
            message(LOG_INFO, "can't sleep\n");
            break;
    }
}

static void initializePowerNotifications(void) {
    static io_connect_t rootPort;   /* used by powerCallback() via context pointer */

    IONotificationPortRef notificationPort;
    io_object_t notifier;

    rootPort = IORegisterForSystemPower(&rootPort, &notificationPort, powerCallback, &notifier);
    if (!rootPort) {
        message(LOG_ERR, "IORegisterForSystemPower failed\n");
        exit(1);
    }
    CFRunLoopAddSource(
        CFRunLoopGetCurrent(),
        IONotificationPortGetRunLoopSource(notificationPort),
        kCFRunLoopDefaultMode
    );
}

static void signalCallback(int sig) {
    switch (sig) {
        case SIGTERM:
        case SIGINT:
            message(
                LOG_INFO,
                "got %s - exiting\n",
                sig == SIGTERM ? "SIGTERM" : "SIGINT"
            );
            writePidFile(NULL);
            exit(0);
            break;
    }
}


int main (int argc, char * const *argv) {
    parseArgs(argc, argv);
    signal(SIGINT, signalCallback);
    signal(SIGTERM, signalCallback);
    initializePowerNotifications();
    CFRunLoopRun();
    return 0;
}
