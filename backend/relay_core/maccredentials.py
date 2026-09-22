# SPDX-License-Identifier: AGPL-3.0-or-later
"""macOS Keychain generic passwords, through Security.framework (no secret argv).

The login Keychain controls access and persistence. No synchronizable attribute is
set, so Relay does not opt credentials into iCloud synchronization.
"""
from contextlib import contextmanager
import ctypes as C

_CF = C.CDLL('/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation')
_SEC = C.CDLL('/System/Library/Frameworks/Security.framework/Security')
_PTR = C.c_void_p
_INDEX = C.c_long


def _function(lib, name, arguments, result):
    function = getattr(lib, name)
    function.argtypes, function.restype = arguments, result
    return function


_string = _function(_CF, 'CFStringCreateWithCString', [_PTR, C.c_char_p, C.c_uint32], _PTR)
_data = _function(_CF, 'CFDataCreate', [_PTR, _PTR, _INDEX], _PTR)
_dictionary = _function(_CF, 'CFDictionaryCreate',
                        [_PTR, C.POINTER(_PTR), C.POINTER(_PTR), _INDEX, _PTR, _PTR], _PTR)
_release = _function(_CF, 'CFRelease', [_PTR], None)
_length = _function(_CF, 'CFDataGetLength', [_PTR], _INDEX)
_bytes = _function(_CF, 'CFDataGetBytePtr', [_PTR], _PTR)
_read = _function(_SEC, 'SecItemCopyMatching', [_PTR, C.POINTER(_PTR)], C.c_int32)
_add = _function(_SEC, 'SecItemAdd', [_PTR, _PTR], C.c_int32)
_update = _function(_SEC, 'SecItemUpdate', [_PTR, _PTR], C.c_int32)
_delete = _function(_SEC, 'SecItemDelete', [_PTR], C.c_int32)
_KEY_CALLBACKS = C.addressof(C.c_byte.in_dll(_CF, 'kCFTypeDictionaryKeyCallBacks'))
_VALUE_CALLBACKS = C.addressof(C.c_byte.in_dll(_CF, 'kCFTypeDictionaryValueCallBacks'))
_NOT_FOUND, _DUPLICATE = -25300, -25299


def _constant(name):
    return _PTR.in_dll(_SEC, name).value


@contextmanager
def _query(service=None, account=None, *, secret=None, returning=False):
    pairs = []
    owned = []
    dictionary = None
    try:
        if service is not None:
            pairs.append((_constant('kSecClass'), _constant('kSecClassGenericPassword')))
            for key, value in [('kSecAttrService', service), ('kSecAttrAccount', account)]:
                pointer = _string(None, value.encode('utf-8'), 0x08000100)
                if not pointer:
                    raise MemoryError('Could not allocate Keychain query')
                owned.append(pointer)
                pairs.append((_constant(key), pointer))
        if secret is not None:
            encoded = secret.encode('utf-8')
            pointer = _data(None, encoded, len(encoded))
            if not pointer:
                raise MemoryError('Could not allocate Keychain data')
            owned.append(pointer)
            pairs.append((_constant('kSecValueData'), pointer))
        if returning:
            pairs.append((_constant('kSecReturnData'), _PTR.in_dll(_CF, 'kCFBooleanTrue').value))
            pairs.append((_constant('kSecMatchLimit'), _constant('kSecMatchLimitOne')))
        keys = (_PTR * len(pairs))(*(pair[0] for pair in pairs))
        values = (_PTR * len(pairs))(*(pair[1] for pair in pairs))
        dictionary = _dictionary(None, keys, values, len(pairs), _KEY_CALLBACKS, _VALUE_CALLBACKS)
        if not dictionary:
            raise MemoryError('Could not allocate Keychain dictionary')
        yield dictionary
    finally:
        if dictionary:
            _release(dictionary)
        for pointer in owned:
            _release(pointer)


def _check(status):
    if status:
        # Only the OSStatus is diagnostic; never interpolate values or credential data.
        raise OSError(status, 'macOS Keychain could not complete the request')


def lookup(service, account):
    result = _PTR()
    with _query(service, account, returning=True) as query:
        status = _read(query, C.byref(result))
    if status == _NOT_FOUND:
        return ''
    _check(status)
    try:
        return C.string_at(_bytes(result), _length(result)).decode('utf-8')
    finally:
        if result:
            _release(result)


def store(service, account, value):
    with _query(service, account, secret=value) as query:
        status = _add(query, None)
    if status == _DUPLICATE:
        with _query(service, account) as query, _query(secret=value) as attributes:
            status = _update(query, attributes)
    _check(status)


def remove(service, account):
    with _query(service, account) as query:
        status = _delete(query)
    if status == _NOT_FOUND:
        return False
    _check(status)
    return True
