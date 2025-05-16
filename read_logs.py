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


class Maintenance(enum.Enum):

    SERVICE_UUID = '6acc55c0-e631-4069-944d-b8ca7598ad50'

    LOG_MODE = '6acc55c1-e631-4069-944d-b8ca7598ad50'
    LOG_SIZE = '6acc55c2-e631-4069-944d-b8ca7598ad50'
    LOG_BLOCK = '6acc55c3-e631-4069-944d-b8ca7598ad50'


async def read_logs():
    # print('Getting key from vanmoof servers')
    # key, user_key_id = retrieve_encryption_key.query()

    # Insert your API key here:
    key = '5f5f5f5f5f4f574e45525f5045524d53'
    user_key_id = 1
    print(f'key: {key}, user_key_id {user_key_id}')

    print('Discovering nearby vanmoof bikes')
    device = await discover_bike.query()
    if device is None:
        exit(1)

    print('Reading logs')
    async with bleak.BleakClient(device) as bleak_client:
        client = SX3Client(bleak_client, key, user_key_id)

        await client.authenticate()

        await client.play_sound(Sound.BEEP_POSITIVE)

        result = await client._read(Maintenance.LOG_MODE)
        mode = int(result[0])
        print(f'Log mode: {mode}')
        result = await client._read(Maintenance.LOG_SIZE)
        size = int.from_bytes(result[0:4], "big")
        print(f'Log size: {size}')

        log_data = ''
        offset = 0

        try:
            print('Log data:')
            while offset < size:
                block = bytearray()
                block.append((offset >> 24) & 0xff)
                block.append((offset >> 16) & 0xff)
                block.append((offset >> 8) & 0xff)
                block.append(offset & 0xff)
                block.append(255) # Number of blocks
                await client._write(Maintenance.LOG_BLOCK, block)
                result = await client._read(Maintenance.LOG_BLOCK)
                text = result.decode('ASCII')
                print(text, end='')
                log_data += text
                offset += int(len(result) / 16)
            print('')
        except Exception as e:
            print(f'Error reading logs: {e}, last offset {offset}')


asyncio.run(read_logs())
