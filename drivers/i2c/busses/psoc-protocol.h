#ifndef __PSOC_PROTOCOL_H__
#define __PSOC_PROTOCOL_H__

/** Escaped commands protocol
 * each command must be preceded by ESCAPE_CHAR
 * each command is 1 byte long, but can have parameters
 * non-escaped data will be treated differently depending on the last received command
 */
#define ESCAPE_CHAR          27  // 'ESC' ASCII standard value (Data Link Escape)

/** Commands
 * commands defines are in the form CMD_<NAME>_<Subfield>
 * serial communication should only use CMD_<NAME> characters escaped with ESCAPE_CHAR
 * CMD_<NAME>_<Subfield> constants are used internally for the FSM and reports status
 */

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * escape mode commands, and sub-states
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/* ESCAPED_DATA
 * data/register values must be escaped, this is considered as a command
 * */
#define CMD_LITTERAL         ESCAPE_CHAR // insert litteral escape char in any stream

/* I2C_MASTER_RECV
 *   [Esc] 'r' @ N
 *   [Esc] 'n' @ N (no stop)
 * */
#define CMD_READ             'r' // (=114) I2C-style read
#define CMD_READ_NOSTOP      'n' // (=110) I2C-style read, no stop at end but restart and chained write following
#define CMD_READ_Address     178 // (=114+64) I2C-style read address parameter
#define CMD_READ_Number      242 // (=114+128) I2C-style read data length

/* I2C_MASTER_SEND
 *   [Esc] 'S' @ d d d ... [Esc][Eot]
 *   [Esc] 'N' @ d d d ... [Esc][Eot] (no stop)
 * */
#define CMD_WRITE            'S' // (= 83) I2C-style write
#define CMD_WRITE_NOSTOP     'N' // (= 78) I2C-style write, no stop at end : restart and chained read command
#define CMD_WRITE_Address    147 // (=83+64) I2C-style write address parameter
#define CMD_WRITE_eData      192 // (=192) I2C-style write escaped data

/* I2C_TRANSFER
 * is handled with 2 I2C_MASTER_* commands, second being without stop
 * [Esc]'r' @ N [Esc]'N' @ d d d ... [Esc][Eot]
 * */

#define CMD_IGNORE           'x' // (=120) Abort command : Ignore any character til next valid command (fallback mode)

#define CMD_EOT               4  // (=  4) [Eot] End of Transmission (Standard ASCII value)
//used in I2C_MASTER_SEND, I2C_TRANSFER, SMBUS_BLOCK_PROC_CALL, SMBUS_WRITE_BLOCK_DATA, SMBUS_WRITE_I2C_BLOCK

#define CMD_VERSION          'v' // (=118) Request for version string
#define CMD_WAKEUP           '+'  // (=43) sent by the psoc to try to unlock the serial driver

//Debug features
#define CMD_DBGECHO          'e' // (=101) RX->TX echo toggle
#define CMD_DBGSR            'i' // (=105) toggle status report (noisy)
#define CMD_DBGINFO          'I' // (= 73) health status info
#define CMD_RESET            'z' // (=122) Empty all buffers, reset status...
#define CMD_DBGALARM         'Z' // (= 90) health status info

//TODO
/* SMBUS_READ_BLOCK_DATA
 *   [Esc]'k' @ c (reply includes number of bytes)
 * SMBUS_READ_I2C_BLOCK
 *   [Esc]'k' @ c
 * */
#define CMD_SMBLOCKREAD      'k' // (=107) SMBus-style bloc'K' read
#define CMD_SMBLOCKWRITE     'K' // (= 75) SMBus-style bloc'K' write
#define CMD_SMBYTEREAD       'b' // (= 98) SMBus-style 'B'yte read
#define CMD_SMBYTEWRITE      'B' // (= 66) SMBus-style 'B'yte write
#define CMD_SMBYTEDATAREAD   'd' // (= 98) SMBus-style byte read with register 'D'ata byte
#define CMD_SMBYTEDATAWRITE  'D' // (= 66) SMBus-style byte write with register 'D'ata byte
#define CMD_SMBITREAD        'q' // (=113) SMBus-style bit read / Writequick(1)
#define CMD_SMBITWRITE       'Q' // (= 81) SMBus-style bit write / Writequick(0)
#define CMD_PING             'q' // (=113) 0 byte read to ping an I2C slave
#define CMD_ACK               6  // (=  6) ASCII 'ACK' : reply by <ESC> <ACK> inserted as soon as possible in upcoming stream

//Error replies from PSoC
#define REPLY_OVERFLOW       'o' // (=111)
#define REPLY_TIMEOUT        't' // (=116)
#define REPLY_CANCELWRITE    'C' // (= 67) Write command not accepted (busy)
#define REPLY_CANCELREAD     'c' // (= 99) Read command refused
// I2C Bus Errors
#define REPLY_ADDRNAK        'N' // (= 78)
#define REPLY_NAK            'n' // (=110)
#define REPLY_STALLED        'x' // (=120) Bus stalled

#endif //__PSOC_PROTOCOL_H__
