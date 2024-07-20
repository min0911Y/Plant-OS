#pragma once

#define SB16_IRQ                           5
#define SB16_FAKE_TID                      -3
#define SB16_PORT_MIXER                    0x224
#define SB16_PORT_DATA                     0x225
#define SB16_PORT_RESET                    0x226
#define SB16_PORT_READ                     0x22A
#define SB16_PORT_WRITE                    0x22C
#define SB16_PORT_READ_STATUS              0x22E
#define SB16_PORT_DSP_16BIT_INTHANDLER_IRQ 0x22F
#define COMMAND_DSP_WRITE                  0x40
#define COMMAND_DSP_SOSR                   0x41
#define COMMAND_DSP_TSON                   0xD1
#define COMMAND_DSP_TSOF                   0xD3
#define COMMAND_DSP_STOP8                  0xD0
#define COMMAND_DSP_RP8                    0xD4
#define COMMAND_DSP_STOP16                 0xD5
#define COMMAND_DSP_RP16                   0xD6
#define COMMAND_DSP_VERSION                0xE1
#define COMMAND_MIXER_MV                   0x22
#define COMMAND_SET_IRQ                    0x80
#define BUF_RDY_VAL                        128