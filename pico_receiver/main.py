"""MicroPython: Raspberry Pi Pico + Waveshare Pico-LoRa-SX1262 (868 MHz).

Receptor LoRa P2P de paquetes de texto enviados por el STM32.
Adaptacion propia de comandos SX1262, sin dependencias externas.
"""
from machine import Pin, SPI
import time

FREQUENCY_HZ = 868100000
IRQ_RX_DONE = 0x0002
IRQ_HEADER_ERROR = 0x0020
IRQ_CRC_ERROR = 0x0040
IRQ_TIMEOUT = 0x0200


class SX1262Receiver:
    def __init__(self):
        self.cs = Pin(3, Pin.OUT, value=1)
        self.reset_pin = Pin(15, Pin.OUT, value=1)
        self.busy = Pin(2, Pin.IN)
        self.dio1 = Pin(20, Pin.IN)
        self.spi = SPI(1, baudrate=500000, polarity=0, phase=0,
                       bits=8, firstbit=SPI.MSB,
                       sck=Pin(10), mosi=Pin(11), miso=Pin(12))

    def wait_busy(self):
        start = time.ticks_ms()
        while self.busy.value():
            if time.ticks_diff(time.ticks_ms(), start) >= 1000:
                raise RuntimeError("BUSY alto: revisar alimentacion y conexiones")
            time.sleep_ms(1)

    def write(self, opcode, data=b""):
        self.wait_busy()
        self.cs.value(0)
        try:
            self.spi.write(bytes([opcode]) + data)
        finally:
            self.cs.value(1)
        time.sleep_us(1)  # Allow BUSY assertion after NSS rises.
        self.wait_busy()

    def read(self, command, count):
        self.wait_busy()
        self.cs.value(0)
        try:
            self.spi.write(command)
            result = self.spi.read(count, 0x00)
        finally:
            self.cs.value(1)
        self.wait_busy()
        return result

    def read_register(self, address, count=1):
        return self.read(bytes([0x1D, address >> 8, address & 255, 0]), count)

    def write_register(self, address, data):
        self.write(0x0D, bytes([address >> 8, address & 255]) + data)

    def status(self):
        # GetStatus is two bytes total; status arrives during the NOP.
        return self.read(b"\xC0", 1)[0]

    def errors(self):
        value = self.read(b"\x17\x00", 2)
        return (value[0] << 8) | value[1]

    def clear_irq(self):
        self.write(0x02, b"\xFF\xFF")

    def start_rx(self):
        self.write(0x82, b"\xFF\xFF\xFF")  # Continuous RX.

    def initialize(self):
        self.cs.value(1)
        self.reset_pin.value(0)
        time.sleep_ms(20)
        self.reset_pin.value(1)
        time.sleep_ms(20)
        self.wait_busy()
        self.write(0x80, b"\x00")  # Standby RC.
        self.write(0x07, b"\x00\x00")  # ClearDeviceErrors.
        self.write(0x97, b"\x01\x00\x01\x40")  # TCXO 1.7 V / 320 ticks = 5 ms.
        self.write(0x89, b"\x7F")  # Calibrate all blocks.
        self.write(0x80, b"\x01")  # Standby XOSC.
        status, errors = self.status(), self.errors()
        print("[PICO] XOSC status=0x%02X errors=0x%04X" % (status, errors))
        command_status = (status >> 1) & 7
        if ((status >> 4) & 7) != 3 or errors or command_status not in (1, 2, 6):
            raise RuntimeError("XOSC no disponible; RX cancelado")

        self.write(0x9D, b"\x01")  # DIO2 controls onboard RF switch.
        self.write(0x8A, b"\x01")  # LoRa packet type.
        self.write(0x98, b"\xD7\xDB")  # Image calibration for 863-870 MHz.
        self.write(0x86, ((FREQUENCY_HZ << 25) // 32000000).to_bytes(4, "big"))
        self.write(0x8B, b"\x07\x04\x01\x00")  # SF7 / BW125 / CR4_5 / LDRO off.
        # Preamble=8, explicit header, max RX length=255, CRC on, normal IQ.
        self.write(0x8C, b"\x00\x08\x00\xFF\x01\x00")
        polarity = self.read_register(0x0736)[0]
        self.write_register(0x0736, bytes([polarity | 0x04]))  # Normal-IQ workaround.
        self.write_register(0x0740, b"\x34\x44")  # Public sync word, matches STM32.
        if self.read_register(0x0740, 2) != b"\x34\x44":
            raise RuntimeError("Verificacion sync word SPI fallida")
        self.write(0x8F, b"\x00\x00")  # Buffer base addresses.
        self.write(0x08, b"\x02\x62\x02\x62\x00\x00\x00\x00")
        if self.errors():
            raise RuntimeError("Error interno despues de configurar RX")
        self.clear_irq()
        self.start_rx()
        status = self.status()
        if ((status >> 4) & 7) != 5 or ((status >> 1) & 7) in (3, 4, 5):
            raise RuntimeError("La radio no entro en RX: 0x%02X" % status)
        print("[PICO] RX listo: 868.1 MHz SF7 BW125 CR4/5 sync=3444 CRC=ON IQ=normal")

    def poll(self):
        value = self.read(b"\x12\x00", 2)
        irq = (value[0] << 8) | value[1]
        if not irq:
            return None
        packet = None
        if irq & (IRQ_CRC_ERROR | IRQ_HEADER_ERROR):
            print("[PICO] Paquete descartado CRC/cabecera irq=0x%04X" % irq)
        elif irq & IRQ_RX_DONE:
            length, offset = self.read(b"\x13\x00", 2)
            packet = self.read(bytes([0x1E, offset, 0]), length)
            metrics = self.read(b"\x14\x00", 3)
            snr = metrics[1] if metrics[1] < 128 else metrics[1] - 256
            try:
                text = packet.decode("utf-8")
            except UnicodeError:
                text = repr(packet)
            print("[RX] %s | len=%d RSSI=%.1f dBm SNR=%.2f dB" %
                  (text, length, -metrics[0] / 2, snr / 4))
        elif irq & IRQ_TIMEOUT:
            print("[PICO] RX timeout; reiniciando escucha")
        self.clear_irq()
        # Re-arm also after a header/CRC error. Receiver is polling, not an ISR.
        self.start_rx()
        return packet


def main():
    radio = SX1262Receiver()
    radio.initialize()
    last_notice = time.ticks_ms()
    while True:
        if radio.poll() is not None:
            last_notice = time.ticks_ms()
        if time.ticks_diff(time.ticks_ms(), last_notice) >= 15000:
            print("[PICO] Esperando paquete P2P; DIO1=%d" % radio.dio1.value())
            last_notice = time.ticks_ms()
        time.sleep_ms(5)


if __name__ == "__main__":
    main()
