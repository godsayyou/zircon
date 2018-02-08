// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include <threads.h>

#include <bits/limits.h>
#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/platform-defs.h>
#include <ddk/protocol/platform-device.h>
#include <ddk/protocol/serial.h>
#include <hw/reg.h>
#include <soc/aml-common/aml-uart.h>

#include <zircon/assert.h>
#include <zircon/threads.h>
#include <zircon/types.h>

// crystal clock speed
#define CLK_XTAL    24000000

#define DEFAULT_BAUD_RATE   115200
#define DEFAULT_CONFIG      (SERIAL_DATA_BITS_8 | SERIAL_STOP_BITS_1 | SERIAL_PARITY_NONE)

typedef struct {
    struct aml_uart* uart;
    uint32_t port_num;
    pdev_vmo_buffer_t mmio;
    zx_handle_t irq_handle;
    thrd_t irq_thread;
    serial_state_cb notify_cb;
    void* cookie;
    uint32_t state; // last state we sent to notify_cb
    mtx_t lock;
    bool enabled;
} aml_uart_port_t;

typedef struct aml_uart {
    platform_device_protocol_t pdev;
    serial_driver_protocol_t serial;
    zx_device_t* zxdev;
    aml_uart_port_t* ports;
    unsigned port_count;
} aml_uart_t;

static uint32_t aml_uart_read_state(aml_uart_port_t* port) {
    void* mmio = port->mmio.vaddr;
    volatile uint32_t* status_reg = mmio + AML_UART_STATUS;
    uint32_t status = readl(status_reg);

    mtx_lock(&port->lock);

    uint32_t state = 0;
    if (!(status & AML_UART_STATUS_RXEMPTY)) {
        state |= SERIAL_STATE_READABLE;
    }
    if (!(status & AML_UART_STATUS_TXFULL)) {
        state |= SERIAL_STATE_WRITABLE;
    }
    bool notify = (state != port->state);
    port->state = state;
    
    mtx_unlock(&port->lock);
    
    if (notify && port->notify_cb) {
        port->notify_cb(port->port_num, state, port->cookie);
    }

    return state;
}

static int aml_uart_irq_thread(void *arg) {
    zxlogf(INFO, "aml_uart_irq_thread start\n");

    aml_uart_port_t* port = arg;
    void* mmio = port->mmio.vaddr;
    volatile uint32_t* ctrl_reg = mmio + AML_UART_CONTROL;
    volatile uint32_t* irq_ctrl_reg = mmio + AML_UART_MISC;

    // reset the port
    uint32_t temp = readl(ctrl_reg);

    temp |= AML_UART_CONTROL_RSTRX | AML_UART_CONTROL_RSTTX | AML_UART_CONTROL_CLRERR;
    writel(temp, ctrl_reg);
    temp &= ~(AML_UART_CONTROL_RSTRX | AML_UART_CONTROL_RSTTX | AML_UART_CONTROL_CLRERR);
    writel(temp, ctrl_reg);

    // enable rx and tx
    temp |= AML_UART_CONTROL_TXEN | AML_UART_CONTROL_RXEN;
    temp |= AML_UART_CONTROL_TXINTEN | AML_UART_CONTROL_RXINTEN;
    writel(temp, ctrl_reg);

    // Set to interrupt every 1 tx and rx byte
// FIXME use defines
    temp = readl(irq_ctrl_reg);
    temp &= 0xffff0000;
// FIXME tx threshold should be half of fifo size?
    temp |= (1 << 8) | ( 1 );
    writel(temp, irq_ctrl_reg);

/*
printf("REGS for port %u\n", port->port_num);
printf("    AML_UART_CONTROL: %08x\n", readl(mmio + AML_UART_CONTROL));
printf("    AML_UART_STATUS:  %08x\n", readl(mmio + AML_UART_STATUS));
printf("    AML_UART_MISC:    %08x\n", readl(mmio + AML_UART_MISC));
printf("    AML_UART_REG5:    %08x\n", readl(mmio + AML_UART_REG5));
*/

    while (1) {
        uint64_t slots;
        zx_status_t result = zx_interrupt_wait(port->irq_handle, &slots);
        if (result != ZX_OK) {
            zxlogf(ERROR, "aml_uart_irq_thread: zx_interrupt_wait got %d\n", result);
            break;
        }

        aml_uart_read_state(port);
    }

    // disable TX/RX on the way out
    temp = readl(ctrl_reg);
    temp &= ~(AML_UART_CONTROL_TXEN | AML_UART_CONTROL_RXEN);
    writel(temp, ctrl_reg);

// FIXME - assert AML_UART_CONTROL_INVRTS too?

    zxlogf(INFO, "aml_uart_irq_thread done\n");

    return 0;
}

static uint32_t aml_serial_get_port_count(void* ctx) {
    aml_uart_t* uart = ctx;
    return uart->port_count;
}

static zx_status_t aml_serial_config(void* ctx, uint32_t port_num, uint32_t baud_rate, uint32_t flags) {
    aml_uart_t* uart = ctx;
    if (port_num >= uart->port_count) {
        return ZX_ERR_INVALID_ARGS;
    }
    aml_uart_port_t* port = &uart->ports[port_num];
    void* mmio = port->mmio.vaddr;
    volatile uint32_t* ctrl_reg = mmio + AML_UART_CONTROL;
    volatile uint32_t* reg5 = mmio + AML_UART_REG5;

    uint32_t ctrl_bits = 0;
    const uint32_t config_mask = AML_UART_CONTROL_XMITLEN_MASK |
                                 AML_UART_CONTROL_STOPLEN_MASK |
                                 AML_UART_CONTROL_PAR_MASK |
                                 AML_UART_CONTROL_TWOWIRE;

    switch (flags & SERIAL_DATA_BITS_MASK) {
    case SERIAL_DATA_BITS_5:
        ctrl_bits |= AML_UART_CONTROL_XMITLEN_5;
        break;
    case SERIAL_DATA_BITS_6:
        ctrl_bits |= AML_UART_CONTROL_XMITLEN_6;
        break;
    case SERIAL_DATA_BITS_7:
        ctrl_bits |= AML_UART_CONTROL_XMITLEN_7;
        break;
    case SERIAL_DATA_BITS_8:
        ctrl_bits |= AML_UART_CONTROL_XMITLEN_8;
        break;
    default:
        return ZX_ERR_INVALID_ARGS;
    }

    switch (flags & SERIAL_STOP_BITS_MASK) {
    case SERIAL_STOP_BITS_1:
        ctrl_bits |= AML_UART_CONTROL_STOPLEN_1;
        break;
    case SERIAL_STOP_BITS_2:
        ctrl_bits |= AML_UART_CONTROL_STOPLEN_2;
        break;
    default:
        return ZX_ERR_INVALID_ARGS;
    }

    switch (flags & SERIAL_PARITY_MASK) {
    case SERIAL_PARITY_NONE:
        ctrl_bits |= AML_UART_CONTROL_PAR_NONE;
        break;
    case SERIAL_PARITY_EVEN:
        ctrl_bits |= AML_UART_CONTROL_PAR_EVEN;
        break;
    case SERIAL_PARITY_ODD:
        ctrl_bits |= AML_UART_CONTROL_PAR_ODD;
        break;
    default:
        return ZX_ERR_INVALID_ARGS;
    }

    switch (flags & SERIAL_FLOW_CTRL_MASK) {
    case SERIAL_FLOW_CTRL_NONE:
        ctrl_bits |= AML_UART_CONTROL_TWOWIRE;
    case SERIAL_FLOW_CTRL_CTS_RTS:
        // CTS/RTS is on by default
        break;
    default:
        return ZX_ERR_INVALID_ARGS;
    }

    uint32_t baud_bits = (CLK_XTAL / 3) / baud_rate - 1;
    if (baud_bits & ~AML_UART_REG5_NEW_BAUD_RATE_MASK) {
        zxlogf(ERROR, "aml_serial_config: baud rate %u too large\n", baud_rate);
        return ZX_ERR_OUT_OF_RANGE;
    }

    mtx_lock(&port->lock);

    uint32_t temp = readl(ctrl_reg);
    temp = 0x120;
    temp &= ~config_mask;
    temp |= ctrl_bits;
    writel(temp, ctrl_reg);
//    writel(ctrl_bits, ctrl_reg);

    temp = baud_bits | AML_UART_REG5_USE_XTAL_CLK | AML_UART_REG5_USE_NEW_BAUD_RATE;
    writel(temp, reg5);

    mtx_unlock(&port->lock);

    return ZX_OK;
}

static zx_status_t aml_serial_enable(void* ctx, uint32_t port_num, bool enable) {
    aml_uart_t* uart = ctx;
    if (port_num >= uart->port_count) {
        return ZX_ERR_INVALID_ARGS;
    }
    aml_uart_port_t* port = &uart->ports[port_num];

    mtx_lock(&port->lock);

    if (enable && !port->enabled) {
        zx_status_t status = pdev_map_interrupt(&port->uart->pdev, port_num, &port->irq_handle);
        if (status != ZX_OK) {
            zxlogf(ERROR, "aml_serial_enable: pdev_map_interrupt failed %d\n", status);
            mtx_unlock(&port->lock);
            return status;
        }

        int rc = thrd_create_with_name(&port->irq_thread, aml_uart_irq_thread, port,
                                       "aml_uart_irq_thread");
        if (rc != thrd_success) {
            zx_handle_close(port->irq_handle);
            port->irq_handle = ZX_HANDLE_INVALID;
            mtx_unlock(&port->lock);
            return thrd_status_to_zx_status(rc);
        }
    } else if (!enable && port->enabled) {
        zx_interrupt_signal(port->irq_handle, ZX_INTERRUPT_SLOT_USER, 0);
        thrd_join(port->irq_thread, NULL);
        zx_handle_close(port->irq_handle);
        port->irq_handle = ZX_HANDLE_INVALID;
    }

    port->enabled = enable;
    mtx_unlock(&port->lock);

    return ZX_OK;
}

static zx_status_t aml_serial_read(void* ctx, uint32_t port_num, void* buf, size_t length,
                                   size_t* out_actual) {
    aml_uart_t* uart = ctx;
    if (port_num >= uart->port_count) {
        return ZX_ERR_INVALID_ARGS;
    }

    aml_uart_port_t* port = &uart->ports[port_num];
    void* mmio = port->mmio.vaddr;
    volatile uint32_t* rfifo_reg = mmio + AML_UART_RFIFO;

    uint8_t* bufptr = buf;
    uint8_t* end = bufptr + length;
// need to lock around reading status to avoid problems with aml_serial_write?

    while (bufptr < end && (aml_uart_read_state(port) & SERIAL_STATE_READABLE)) {
        uint32_t val = readl(rfifo_reg);
        *bufptr++ = val;
    }

    size_t read = (void *)bufptr - buf;
    if (read == 0) {
        return ZX_ERR_SHOULD_WAIT;
    }
    *out_actual = read;
    return ZX_OK;
}

static zx_status_t aml_serial_write(void* ctx, uint32_t port_num, const void* buf, size_t length,
                                    size_t* out_actual) {
printf("aml_serial_write:\n");
for (size_t i = 0; i < length; i++) {
printf(" %02x", ((char *)buf)[i]);
}
printf("\n");

    aml_uart_t* uart = ctx;
    if (port_num >= uart->port_count) {
        return ZX_ERR_INVALID_ARGS;
    }
    
    aml_uart_port_t* port = &uart->ports[port_num];
    void* mmio = port->mmio.vaddr;
    volatile uint32_t* wfifo_reg = mmio + AML_UART_WFIFO;

    const uint8_t* bufptr = buf;
    const uint8_t* end = bufptr + length;
    while (bufptr < end && (aml_uart_read_state(port) & SERIAL_STATE_WRITABLE)) {
        writel(*bufptr++, wfifo_reg);
    }

    size_t written = (void *)bufptr - buf;
    if (written == 0) {
        return ZX_ERR_SHOULD_WAIT;
    }
    *out_actual = written;
    return ZX_OK;
}

static zx_status_t aml_serial_set_notify_callback(void* ctx, uint32_t port_num, serial_state_cb cb,
                                                  void* cookie) {
    aml_uart_t* uart = ctx;
    if (port_num >= uart->port_count) {
        return ZX_ERR_INVALID_ARGS;
    }
    aml_uart_port_t* port = &uart->ports[port_num];

    port->notify_cb = cb;
    port->cookie = cookie;

    // this will trigger notifying current state
    aml_uart_read_state(port);

    return ZX_OK;
}

static serial_driver_ops_t aml_serial_ops = {
    .get_port_count = aml_serial_get_port_count,
    .config = aml_serial_config,
    .enable = aml_serial_enable,
    .read = aml_serial_read,
    .write = aml_serial_write,
    .set_notify_callback = aml_serial_set_notify_callback,
};

static void aml_uart_release(void* ctx) {
    aml_uart_t* uart = ctx;
    for (unsigned i = 0; i < uart->port_count; i++) {
        aml_uart_port_t* port = &uart->ports[i];
// TODO cancel and thread join
        pdev_vmo_buffer_release(&port->mmio);
        zx_handle_close(port->irq_handle);
    }
    free(uart->ports);
    free(uart);
}

static zx_protocol_device_t uart_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .release = aml_uart_release,
};

static zx_status_t aml_uart_bind(void* ctx, zx_device_t* parent) {
    zx_status_t status;

    aml_uart_t* uart = calloc(1, sizeof(aml_uart_t));
    if (!uart) {
        return ZX_ERR_NO_MEMORY;
    }

    if ((status = device_get_protocol(parent, ZX_PROTOCOL_PLATFORM_DEV, &uart->pdev)) != ZX_OK) {
        zxlogf(ERROR, "aml_uart_bind: ZX_PROTOCOL_PLATFORM_DEV not available\n");
        goto fail;
    }

    platform_bus_protocol_t pbus;
    if ((status = device_get_protocol(parent, ZX_PROTOCOL_PLATFORM_BUS, &pbus)) != ZX_OK) {
        zxlogf(ERROR, "aml_uart_bind: ZX_PROTOCOL_PLATFORM_BUS not available\n");
        goto fail;
    }

    pdev_device_info_t info;
    status = pdev_get_device_info(&uart->pdev, &info);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml_uart_bind: pdev_get_device_info failed\n");
        goto fail;
    }
    if (info.mmio_count != info.irq_count) {
         zxlogf(ERROR, "aml_uart_bind: mmio_count %u does not match irq_count %u\n",
               info.mmio_count, info.irq_count);
        status = ZX_ERR_INVALID_ARGS;
        goto fail;
    }

    unsigned port_count = info.mmio_count;
    aml_uart_port_t* ports = calloc(port_count, sizeof(aml_uart_port_t));
    if (!ports) {
        status = ZX_ERR_NO_MEMORY;
        goto fail;
    }
    uart->ports = ports;
    uart->port_count = port_count;

    for (unsigned i = 0; i < port_count; i++) {
        aml_uart_port_t* port = &ports[i];
        port->uart = uart;
        mtx_init(&port->lock, mtx_plain);
        port->port_num = i;

        status = pdev_map_mmio_buffer(&uart->pdev, i, ZX_CACHE_POLICY_UNCACHED_DEVICE, &port->mmio);
        if (status != ZX_OK) {
            zxlogf(ERROR, "aml_uart_bind: pdev_map_mmio_buffer failed %d\n", status);
            goto fail;
        }

void* mmio = port->mmio.vaddr;
volatile uint32_t* ctrl_reg = mmio + AML_UART_CONTROL;
writel(0, ctrl_reg);

        aml_serial_config(uart, i, DEFAULT_BAUD_RATE, DEFAULT_CONFIG);
   }

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "aml-uart",
        .ctx = uart,
        .ops = &uart_device_proto,
        .flags = DEVICE_ADD_NON_BINDABLE,
    };

    status = device_add(parent, &args, &uart->zxdev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml_uart_bind: device_add failed\n");
        goto fail;
    }

    uart->serial.ops = &aml_serial_ops;
    uart->serial.ctx = uart;
    pbus_set_protocol(&pbus, ZX_PROTOCOL_SERIAL_DRIVER, &uart->serial);

    return ZX_OK;

fail:
    aml_uart_release(uart);
    return status;
}

static zx_driver_ops_t aml_uart_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = aml_uart_bind,
};

ZIRCON_DRIVER_BEGIN(aml_uart, aml_uart_driver_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PLATFORM_DEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_AMLOGIC),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_AMLOGIC_UART),
ZIRCON_DRIVER_END(aml_uart)