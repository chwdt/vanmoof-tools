import sys
import asyncio

import bleak
import zlib

from pymoof.clients.sx3 import Sound
from pymoof.clients.sx3 import SX3Client
from pymoof.tools import discover_bike
from pymoof.tools import retrieve_encryption_key
from pymoof.util import bleak_utils

import enum
import math

from cryptography.hazmat.primitives.ciphers import algorithms
from cryptography.hazmat.primitives.ciphers import Cipher
from cryptography.hazmat.primitives.ciphers import modes


class Firmware(enum.Enum):
    SERVICE_UUID = '6acc5510-e631-4069-944d-b8ca7598ad50'

    METADATA = '6acc5511-e631-4069-944d-b8ca7598ad50'
    BLOCK = '6acc5512-e631-4069-944d-b8ca7598ad50'
    UNKNOWN_13 = '6acc5513-e631-4069-944d-b8ca7598ad50'


async def firmware_update(client, key, name):
    with open(name, 'rb') as f:
        data = f.read()

    length = len(data)
    crc = zlib.crc32(data) & 0xffffffff

    print(f'file {name}: len {length} crc {hex(crc)}')

    cipher = Cipher(algorithms.AES(bytes.fromhex(key)), modes.ECB())
    encryptor = cipher.encryptor()

    metadata = bytearray()
    metadata.append(0)
    metadata.append((length >> 24) & 0xff)
    metadata.append((length >> 16) & 0xff)
    metadata.append((length >>  8) & 0xff)
    metadata.append((length >>  0) & 0xff)
    metadata.append((crc >> 24) & 0xff)
    metadata.append((crc >> 16) & 0xff)
    metadata.append((crc >>  8) & 0xff)
    metadata.append((crc >>  0) & 0xff)
    print(f'metadata: {metadata.hex()}')

    await client._write(Firmware.METADATA, metadata)

    # Pad to the nearest cipher 16 byte block size
    pad = bytearray()
    for _ in range(math.ceil(len(data) / 16) * 16 - len(data)):
            pad.append(0xff)
    data = data + pad

    enc = bytes(encryptor.update(data) + encryptor.finalize())
    length = len(enc)

    offset = 0
    while offset < length:
        chunk = enc[offset:offset+240]
        print(f'chunk {offset}/{length}: {len(chunk)} {chunk.hex()}')

        await bleak_utils.write_to_characteristic(client._gatt_client, Firmware.BLOCK, chunk)

        offset += 240


async def update():
    # print('Getting key from vanmoof servers')
    # key, user_key_id = retrieve_encryption_key.query()

    # Insert your API key here:
    key = '5f5f5f5f5f4f574e45525f5045524d53'

    user_key_id = 1
    print(f'key: {key}, user_key_id {user_key_id}')

    # Insert your manufacturer key here:
    mkey = '4638384135453030303030304d4f4f46'

    print('Discovering nearby vanmoof bikes')
    device = await discover_bike.query()
    if device is None:
        exit(1)

    print('Doing firmware update')
    async with bleak.BleakClient(device) as bleak_client:
        client = SX3Client(bleak_client, key, user_key_id)

        await client.authenticate()

        await client.play_sound(Sound.BEEP_POSITIVE)

        await firmware_update(client, mkey, sys.argv[1])


asyncio.run(update())
