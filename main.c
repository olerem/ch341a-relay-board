/* 
 * inspired by : usb-relay - a tiny control program for a CH341A based relay board.
 * Copyright (C) 2010  Henning Rohlfs GPL2 license
 * This version is by Edwin van den Oetelaar (2013/03/11)
 * used for Relay control on Diana2-alpha audio mp3/ogg/aac streamers
 * and Diana-7 streamer (h264/aac) video streamer as sold by www.Schaapsound.nl
 *
 * stand alone C program using libusb-1.0 on linux
 * comes with simple Makefile, make sure that libusb-1.0-dev is installed
 * pkg-config --cflags libusb-1.0 
 */

#include <assert.h>
#include <libusb.h>
#include <malloc.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <unistd.h>
#include <syslog.h>
#include <sys/ioctl.h>
#include <sys/inotify.h>
#include <errno.h>
#include <sys/stat.h>
#include "main.h"
#include "logging.h"

/* Control IO via existence of files in Temp directory 
 * External programs can easily monitor this using inotify scripts
 * every physical IO device has its own directory eg. XXX in this example
 * /tmp/XXX/D_IN_01 .. D_IN_99
 * /tmp/XXX/D_OUT_01 .. D_OUT_99
 * create a file by script or other means and the IO pin will change
 * if an input in changes, a file will be created/removed to reflect status
 */

#define EVENT_SIZE  ( sizeof (struct inotify_event) )
#define EVENT_BUF_LEN     ( 1024 * ( EVENT_SIZE + 16 ) )

static const int FIRST_RELAY_NO = 1;
static const int LAST_RELAY_NO = 8;

typedef enum device_brand
{
    ABACOM = 0, ELOMAX = 1, DEVICE_BRAND_LAST
} device_brand_t;

/* For API documentation see iosolution.h */
/* I2CSolution van Elomax is USB device */

static const uint16_t vid_table[] = {0x1a86, 0x07a0};
static const uint16_t pid_table[] = {0x5512, 0x1008};

typedef struct
{
    uint32_t active_relays; // bit mask requested 
    uint32_t outputbits; // bit mask set
    uint8_t data[8]; // buf for Elomax

    libusb_context *usb_context; // pointer to usb context
    libusb_device_handle *device_handle; // pointer to the usb device handle
    device_brand_t device_brand; /* 0 = ch341a 1= Elomax IOsolutions I2c device */

    /* flag when output needs to be sent, but is not yet done (retry later ?) */
    int output_pending; // cleared by write success 
    // int verbose; // verbose output to console
    int use_syslog; // use syslog for logging instead of console
    int run_as_daemon; // run as daemon, use /tmp/ID/D_OUT_99 inotify for control
    char *event_dir; // where to listen and send events
} ios_handle_t;

/* declaration */
void USB_close_device(ios_handle_t *h);
int USB_open_device(ios_handle_t *handle, uint16_t VID, uint16_t PID);
int USB_setup_device(ios_handle_t *handle);
int USB_write_IO(ios_handle_t *handle);
int run_as_daemon(ios_handle_t *h);
int run_once(ios_handle_t *h, int argc, char *argv[]);

/* implementation */
static int
ios_send(ios_handle_t *handle)
{
    /* bmRequest Type	 
     * Bit 7: Request direction (0=Host to device – Out, 1=Device to host – In).
     * Bits 5-6: Request type (0=standard, 1=class, 2=vendor, 3=reserved).
     * Bits 0-4: Recipient (0=device, 1=interface, 2=endpoint, 3=other).
     *
     * int usb_control_msg(
     * usb_dev_handle *dev,
     * int requesttype, 0x21 see doc
     * int request, 0x09 (set configuration)
     * int value, (??)
     * int index, (??)
     * char *bytes, (data)
     * int size, (number of bytes in data)
     * int timeout); (milli seconds)
     * */
    //libusb_set_configuration()
    /* 0x21 Byte : 0010 0001 , class, interface, host to device */
    if (NULL == handle->device_handle) {
        fprintf(stderr, "could not send, handle==null\n");
        return (-1);
    }
    static const int packet_len = 8;
    int writen_size = libusb_control_transfer(
                                              handle->device_handle, 0x21,
                                              LIBUSB_REQUEST_SET_CONFIGURATION,
                                              0x00, 0,
                                              handle->data, packet_len,
                                              100);

    if (writen_size != packet_len) {
        fprintf(stderr, "Failed to send all the byte of the packet (%i)\n", writen_size);

    }

    return writen_size;
}

int
USB_setup_device(ios_handle_t *handle)
{
    /* the elomax device needs some setup before accepting commands */
    assert(handle);
    assert(handle->device_brand < DEVICE_BRAND_LAST);

    switch (handle->device_brand) {
    case ABACOM:
        lwsl_debug("ABACOM, nothing to do, USB_setup_device()\n");
        break;
    case ELOMAX:
        /* Elomax setup */
        lwsl_debug("ELOMAX, USB_setup_device() enable pull ups\n");
        /* setup the pull up resistors */
        handle->data[0] = 0x55;
        handle->data[1] = 0xFF;
        handle->data[2] = 0xFF;
        int r;
        r = ios_send(handle);
        if (r < 0) {
            if (handle->device_handle != NULL) {
                libusb_close(handle->device_handle);
            }
            handle->device_handle = NULL;
            handle->output_pending = 1;
            return -1;
        } else {
            /* success */
            /* check for pending output and do it */
            if (handle->output_pending) {
                if (USB_write_IO(handle) != -1) {
                    return 0;
                } else {
                    return -1;
                }
            }
        }
        break;
    default:
        fprintf(stderr, "can not happen. invalid device brand\n");
    }


    return 0;
}

static int
send_relay_cmd(libusb_device_handle *dev, uint8_t cmd)
{
    static const unsigned char ch341a_cmd_part1[] = {0xa1, 0x6a, 0x1f, 0x00, 0x10};
    static const unsigned char ch341a_cmd_part2[] = {0x3f, 0x00, 0x00, 0x00, 0x00};

    uint8_t buf[32] = {0}; // buf is large enough
    int n = sizeof (ch341a_cmd_part1);
    int m = sizeof (ch341a_cmd_part2);

    /* fill buf with complete message */
    memcpy(buf, ch341a_cmd_part1, n);
    buf[n] = cmd;
    memcpy(buf + n + 1, ch341a_cmd_part2, m);

    /* send message to usb endpoint */
    static const int endpointid = 2; // for some reason
    int numbytes = n + m + 1;
    int actual_length = 0;

    /* do usb action, rv !=0 on error */
    int rv = libusb_bulk_transfer(dev, endpointid, buf, numbytes, &actual_length, 100);


    //for (int i = 0; i < numbytes; i++)
    //    lwsl_debug("pos=%02d val=%02x", i, buf[i]);

    if (rv != 0)
        lwsl_notice("libusb_bulk_transfer() failed");

    // return 0 on successful write
    return (numbytes != actual_length);
}

/* Actual communication with the device and saving the status */
int
USB_write_IO(ios_handle_t *handle)
{
    assert(handle);
    libusb_device_handle *dev = handle->device_handle;
    assert(dev);
    uint8_t active_relays = (uint8_t) handle->active_relays;
    //uint8_t verbose = handle->verbose;

    if (ELOMAX == handle->device_brand) {
        // do the Elomax protocol
        handle->data[0] = 0x4F; /* command for i2csolution */
        handle->data[1] = 0x00; /* port 0 output */
        handle->data[2] = 0x00; /* port 1 output */
        handle->data[3] = 0x00;
        handle->data[4] = 0x00;
        handle->data[5] = 0x00;
        handle->data[6] = 0x00;
        handle->data[7] = 0x00;

        handle->data[1] = (unsigned char) active_relays; /* bitjes van poort 0 */
        handle->data[2] = 0xFF; /* bitjes van poort 1 (inputs) allemaal hoog wegens pullups */

        int r = ios_send(handle);
        if (r < 0) {
            if (handle->device_handle != NULL)
                libusb_close(handle->device_handle);
            handle->device_handle = NULL;
            handle->output_pending = 1;
        } else {
            /* succes, so reset flag */
            handle->outputbits = active_relays; /* keep state here */
            handle->output_pending = 0;
        }

    } else {
        // do the ch341a protocol
        /* Send the command frame */
        if (send_relay_cmd(dev, 0x00)) goto error;
        /* send stuff for every bit in the mask */
        for (uint8_t mask = 128; mask > 0; mask >>= 1) {
            if (active_relays & mask) {
                /* Send "relay on" */
                if (send_relay_cmd(dev, 0x20)) goto error;
                if (send_relay_cmd(dev, 0x28)) goto error;
                if (send_relay_cmd(dev, 0x20)) goto error;
            } else {
                /* Send "relay off" */
                if (send_relay_cmd(dev, 0x00)) goto error;
                if (send_relay_cmd(dev, 0x08)) goto error;
                if (send_relay_cmd(dev, 0x00)) goto error;
            }
        }
        /* End the command frame */
        if (send_relay_cmd(dev, 0x00)) goto error;
        if (send_relay_cmd(dev, 0x01)) goto error;
    }

    /* Remember the status */
    handle->output_pending = 0;
    handle->outputbits = active_relays;
    return 0; // success
error:
    if (handle->device_handle != NULL)
        libusb_close(handle->device_handle);
    handle->device_handle = NULL;
    handle->output_pending = 1;
    return -1; // problems
}

int
USB_open_device(ios_handle_t *handle, uint16_t VID, uint16_t PID)
{
    assert(NULL == handle->device_handle);

    libusb_device **devs = {0}; // to retrieve a list of devices
    libusb_device_handle *udh = NULL;
    libusb_context *ctx = NULL;

    int r = libusb_init(&ctx); // initialize the library for the session we just declared
    handle->usb_context = ctx;

    if (r < 0) {
        lwsl_err("Init Error %d\n", r); // there was an error
        return -1;
    }

    libusb_set_debug(ctx, 3);

    ssize_t cnt = libusb_get_device_list(ctx, &devs); // get the list of devices
    if (cnt < 0) {
        lwsl_err("Get Device Error\n"); // there was an error
        return -1;
    }


    lwsl_info("[%ld] Devices in list.\n", cnt);

    udh = libusb_open_device_with_vid_pid(ctx, VID, PID); // ch341a_USB_VENDOR_ID, ch341a_USB_PROUCT_ID
    if (!udh) {
        lwsl_warn("Cannot open device: libusb %p\n", udh);
        return -1;
    } else {
        lwsl_info("Device is open\n");

        handle->device_handle = udh; // copy for later use
    }

    libusb_free_device_list(devs, 1); // free the list, unref the devices in it

    if (libusb_kernel_driver_active(udh, 0) == 1) { // find out if kernel driver is attached
        lwsl_info("Kernel Driver Active\n");

        if (libusb_detach_kernel_driver(udh, 0) == 0) // detach it
            lwsl_info("Kernel Driver Detached!\n");
        else
            lwsl_info("Kernel Driver Detach failed!\n");

    }

    r = libusb_claim_interface(udh, 0); // claim interface 0 

    if (r < 0) {
        lwsl_info("Cannot Claim Interface : %d\n", r);
        handle->device_handle = NULL;
        handle->output_pending = 1;
        return -1;
    }

    lwsl_info("Claimed Interface\n");

    handle->output_pending = 1;

    return 0; // success
}

void
USB_close_device(ios_handle_t *h)
{
    assert(h);
    assert(h->usb_context);

    if (h->device_handle)
        libusb_close(h->device_handle);

    libusb_exit(h->usb_context);
}

int
run_once(ios_handle_t *h, int argc, char *argv[])
{
    /* just set some outputs on or off and quit */
    for (int i = optind; i < argc; i++) {
        int relay = atoi(argv[i]);
        if (relay < FIRST_RELAY_NO || relay > LAST_RELAY_NO) {
            fprintf(stderr, "error: only give valid relay numbers (1-8) as parameter\n");
            fprintf(stderr, "you can use -v as first option to enable verbose output debugging\n");
            fprintf(stderr, "example: ./%s -v 1 5 7 will switch 1 5 and 7 on the rest will be off\n", argv[0]);
            return 2;
        }
        h->active_relays |= (1 << (relay - 1));
    }
    lwsl_debug("writing byte %d to usb\n", h->active_relays);

    if (0 == USB_open_device(h,
                             vid_table[h->device_brand],
                             pid_table[h->device_brand])) {
        USB_setup_device(h);
        USB_write_IO(h);
        USB_close_device(h);
    } else {
        lwsl_warn("Error : device not open\n");
        return 3;
    }
    return 0;
}

int
run_as_daemon(ios_handle_t *h)
{
    assert(h);
    assert(h->device_brand < DEVICE_BRAND_LAST);

    lwsl_info("Keep Running, daemon not forking, eventpath=%s pid=%d\n",
              h->event_dir, getpid());

    /* connect to USB IO board */
    while (1) {
        if (0 == USB_open_device(h,
                                 vid_table[h->device_brand],
                                 pid_table[h->device_brand])) {
            USB_setup_device(h);
            break;
        } else {
            lwsl_info("IO board not found, try again in 1 sec\n");
            sleep(1);
        }
    }

    /* start the Inotify stuff */
    int fd = 0;
    int length = 0;
    char buffer[EVENT_BUF_LEN] = {0};
    int wd = 0;
    int i = 0;
    unsigned long int eventcounter = 0;

    fd = inotify_init();

    if (fd < 0)
        perror("inotify_init");

    wd = inotify_add_watch(fd, h->event_dir, IN_ALL_EVENTS);

    if (wd < 0)
        perror("inotify_add_watch");

    /* set initial outputs based on stat() of files already present */

    char b[4096] = {0}; /* file name buffer */
    struct stat sb; /* stat result buffer */
    unsigned relaybits = 0; /* bitpattern to set the relays to, clear */

    /* loop over files, stat() files, set bits in pattern */

    for (i = FIRST_RELAY_NO; i < (LAST_RELAY_NO + 1); i++) {
        int len = snprintf(b, sizeof (b), "%s/D_OUT_%d", h->event_dir, i);
        lwsl_debug("stat( %s ) len=%d\n", b, len);
        if (stat(b, &sb) == 0) {
            lwsl_debug("output (%d) ON\n", i);
            relaybits |= (1 << (i - 1));
        } else {
            lwsl_debug("output (%d) OFF\n", i);

        }
    }

    h->active_relays = relaybits;
    USB_write_IO(h);
    /* read to determine the event change happens on “/tmp” directory. 
     * Actually this read blocks until the change event occurs*/

    while (1) {
        length = read(fd, buffer, EVENT_BUF_LEN);

        /*checking for error*/
        if (length < 0) {
            perror("read");
        }

        int i = 0;

        /*actually read return the list of change events happens. 
         * Here, read the change event one by one and process it accordingly.*/
        while (i < length) {
            struct inotify_event *event = (struct inotify_event *) &buffer[i];

            if (event->len) {

                if (event->mask & IN_CREATE) {
                    if (event->mask & IN_ISDIR) {
                        lwsl_debug("New directory %s created.\n", event->name);
                    } else {
                        lwsl_debug("New file %s created.\n", event->name);
                        /* check pattern */
                        int pin = 0;
                        if (sscanf(event->name, "D_OUT_%d", &pin)) {
                            h->active_relays |= 1 << (pin - 1);
                            lwsl_info("set pin=%d HIGH\n", pin);
                            eventcounter++;
                        }
                    }
                } else if (event->mask & IN_DELETE) {
                    if (event->mask & IN_ISDIR) {
                        lwsl_debug("Directory %s deleted.\n", event->name);
                    } else {
                        lwsl_debug("File %s deleted.\n", event->name);
                        /* check pattern */
                        int pin = 0;
                        if (sscanf(event->name, "D_OUT_%d", &pin)) {
                            h->active_relays &= ~(1 << (pin - 1));

                            lwsl_info("set pin=%d LOW\n", pin);
                            eventcounter++;
                        }
                    }
                }
            }
            i += EVENT_SIZE + event->len;
        }
        /* send the pins states to the IO board */
        USB_write_IO(h);

    }
    /*removing the “/tmp” directory from the watch list.*/
    inotify_rm_watch(fd, wd);

    /*closing the INOTIFY instance*/
    close(fd);


    USB_close_device(h);


    return 0;
}

int
main(int argc, char *argv[])
{

    ios_handle_t *h = calloc(1, sizeof (ios_handle_t));
    int rc = 0; // return value to shell
    extern int log_level; //  default is 7
    lwsl_emit = lwsl_emit_stderr; // log to stderr until we change it

    /* 
     * use old style getopt() to be compatible
     * opterr, optopt, optind, optarg are from <unistd.h>
     */

    opterr = 0;
    int c;

    while ((c = getopt(argc, argv, "dhi:sm:z:")) != -1)
        switch (c) {

        case 's':
            h->use_syslog = 1;
            lwsl_emit = lwsl_emit_syslog;
            break;
        case 'd':
            h->run_as_daemon = 1;
            break;
        case 'i':
            h->event_dir = strdup(optarg);
            break;
        case 'h':
            fprintf(stderr, _helptext);
            exit(1);
            break;
        case 'm':
            /* device brand/protocol 0=ch341 1=elomax */
            h->device_brand = atoi(optarg);
            if (h->device_brand >= DEVICE_BRAND_LAST) {
                fprintf(stderr, "devicebrand must be < %d, (ABACOM=0 or Elomax=1)\n", DEVICE_BRAND_LAST);
                abort();
            }
            break;
        case 'z': /* set log level */
            log_level = atoi(optarg);
            if (log_level > 31) {
                // loglevel out of range
                fprintf(stderr, "loglevel (-z %d) out of range\n", log_level);
                fprintf(stderr, "valid levels : ERR = 1, WARN =2, NOTICE=4, INFO=8, DEBUG=16 OR together");
                abort();
            }
            lws_set_log_level(log_level, lwsl_emit_stderr);
            break;

        default:
            fprintf(stderr,"do we get here?\n");
            exit(1);
        }



    if (h->run_as_daemon) {
        /* we keep running until the end of time (or signal) */
        if (0 == h->event_dir) {
            fprintf(stderr, "using /tmp as default event directory\n");
            h->event_dir = strdup("/tmp");
        }
        rc = run_as_daemon(h);
    } else {
        rc = run_once(h, argc, argv);
    }

    free(h);
    return rc;
}