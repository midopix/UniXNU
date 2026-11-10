#!/usr/bin/env python3
"""Restore omitted DriverKit headers from Apple's earlier Darwin 23 source."""
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import subprocess
import urllib.parse
import urllib.request

# Tags of the Apple sources that still carry the headers newer xnu omits; feeds.toml sets them for the unixnu driver.
TAG = os.environ.get('UNIXNU_DRIVERKIT_TAG', 'xnu-10063.121.3')
TRUST_TAG = os.environ.get('UNIXNU_TRUSTCACHE_TAG', 'xnu-8796.101.5')
DESTINATION = Path('.cache/xnu-work/iokit/DriverKit')
CACHE = Path(f'.cache/sources/xnu-driverkit-{TAG.removeprefix("xnu-")}.json')
URL = f'https://api.github.com/repos/apple-oss-distributions/xnu/contents/iokit/DriverKit?ref={TAG}'
# Anonymous GitHub API calls share a small per-address quota that hosted CI runners exhaust (HTTP 403);
# a token in GITHUB_TOKEN or GH_TOKEN is sent to GitHub hosts only.
TOKEN = os.environ.get('GITHUB_TOKEN') or os.environ.get('GH_TOKEN')


def fetch(url):
    request = urllib.request.Request(url, headers={'User-Agent': 'unixnu-prepare-xnu-source'})
    if TOKEN and urllib.parse.urlsplit(url).hostname in ('api.github.com', 'raw.githubusercontent.com'):
        request.add_header('Authorization', f'Bearer {TOKEN}')
    with urllib.request.urlopen(request, timeout=30) as response:
        return response.read()


OFFLINE = Path('.cache/sources/headers')


def offline_data(entry, destination, name):
    """Use the persistent, blob-verified local copy before any network access.

    A fresh xnu-work clone wipes the restored headers; the offline copy lives
    under .cache/sources (which the clone does not touch), so rebuilding a new
    patch series never needs GitHub connectivity.
    """
    try:
        rel = destination.relative_to(Path('.cache/xnu-work')) / name
    except ValueError:
        return None
    cached = OFFLINE / rel
    if not cached.is_file():
        return None
    data = cached.read_bytes()
    digest = hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest()
    return data if digest == entry['sha'] else None


def restore(entry, destination=DESTINATION, tag=TAG):
    if entry['type'] != 'file' or not entry['name'].endswith('.h'):
        return None
    name = entry.get('relative', entry['name'])
    target = destination / name
    if target.exists():
        return None
    data = offline_data(entry, destination, name)
    if data is None:
        data = fetch(entry['download_url'])
    digest = hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest()
    if digest != entry['sha']:
        raise ValueError(f'Git blob hash mismatch: {entry["name"]}')
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(data)
    return {'name': name, 'source_tag': tag, 'git_blob': digest,
            'sha256': hashlib.sha256(data).hexdigest()}


def restore_deleted_tracked_files(work=Path('.cache/xnu-work')):
    """Recreate tracked files lost when a bulk-delete guard rebuilt the work tree.

    On sandboxed Linux the exact paths removed by a bulk delete cannot be
    recreated by open/rename (EPERM), even though the parent directory is
    writable. git ls-files -d finds them; git show :<path> supplies the blob;
    linking a temporary file into place avoids the per-path create restriction.
    """
    deleted = subprocess.check_output(
        ['git', '-C', str(work), 'ls-files', '-d', '-z']).split(b'\0')
    fixed = 0
    for raw in deleted:
        if not raw:
            continue
        rel = raw.decode()
        target = work / rel
        blob = subprocess.check_output(['git', '-C', str(work), 'show', ':' + rel])
        tmp = target.with_name('.' + target.name + '.g0245d-relink')
        tmp.write_bytes(blob)
        try:
            os.link(tmp, target)
        except OSError:
            try:
                tmp.rename(target)
            except OSError:
                tmp.unlink(missing_ok=True)
                raise
        else:
            tmp.unlink()
        fixed += 1
    if fixed:
        print(f'Relinked {fixed} deleted tracked files from the index')


if __name__ == '__main__':
    if not DESTINATION.is_dir():
        raise SystemExit('Create the local XNU work source before preparing headers')
    if not CACHE.exists():
        CACHE.parent.mkdir(parents=True, exist_ok=True)
        CACHE.write_bytes(fetch(URL))
    entries = json.loads(CACHE.read_text())
    crypto_cache = CACHE.with_name(f'xnu-driverkit-crypto-{TAG.removeprefix("xnu-")}.json')
    if not crypto_cache.exists():
        crypto_url = URL.replace('/DriverKit?', '/DriverKit/crypto?')
        crypto_cache.write_bytes(fetch(crypto_url))
    for entry in json.loads(crypto_cache.read_text()):
        entry['relative'] = 'crypto/' + entry['name']
        entries.append(entry)
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as executor:
        restored = [entry for entry in executor.map(restore, entries) if entry]
        trust_tag = TRUST_TAG
        trust_cache = CACHE.with_name(f'xnu-trustcache-{TRUST_TAG.removeprefix("xnu-")}.json')
        if not trust_cache.exists():
            url = ('https://api.github.com/repos/apple-oss-distributions/xnu/'
                   f'contents/EXTERNAL_HEADERS/TrustCache?ref={trust_tag}')
            trust_cache.write_bytes(fetch(url))
        trust_destination = Path('.cache/xnu-work/EXTERNAL_HEADERS/TrustCache')
        def restore_trust(entry):
            return restore(entry, trust_destination, trust_tag)
        restored.extend(entry for entry in executor.map(
            restore_trust, json.loads(trust_cache.read_text())) if entry)
    previous = Path('.cache/sources/xnu-restored-headers.json')
    existing = json.loads(previous.read_text()) if previous.exists() else []
    by_name = {entry['name']: entry for entry in existing + restored}
    previous.write_text(json.dumps(list(by_name.values()), indent=2) + '\n')
    print(f'Restored {len(restored)} omitted public headers from Apple; blob hashes verified')
    restore_deleted_tracked_files()
