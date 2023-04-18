from __future__ import annotations

from base64 import b64encode
from contextlib import asynccontextmanager
from datetime import datetime, timedelta, timezone
from enum import Enum, IntEnum
import functools
import json
from pathlib import Path
import re
from typing import Any, Callable, Dict, Iterable, Literal, Optional, Union

import httpx
import yaml
from trio_websocket import open_websocket_url, WebSocketConnection


class ApiException(RuntimeError):
    pass


class ProjectNotFound(ApiException):
    pass


class DeviceNotFound(ApiException):
    pass


class InvalidObjectID(ApiException):
    pass


class RPCStatusCode(IntEnum):
    OK = 0
    CANCELED = 1
    UNKNOWN = 2
    INVALID_ARGUMENT = 3
    DEADLINE_EXCEEDED = 4
    NOT_FOUND = 5
    ALREADYEXISTS = 6
    PERMISSION_DENIED = 7
    RESOURCE_EXHAUSTED = 8
    FAILED_PRECONDITION = 9
    ABORTED = 10
    OUT_OF_RANGE = 11
    UNIMPLEMENTED = 12
    INTERNAL = 13
    UNAVAILABLE = 14
    DATA_LOSS = 15
    UNAUTHENTICATED = 16


class RPCError(ApiException):
    pass


class RPCResultError(RPCError):
    def __init__(self, status_code: int):
        self.status_code: RPCStatusCode = RPCStatusCode(status_code)
        super().__init__(f'RPC failed with status code {repr(self.status_code)}')


class RPCTimeout(RPCError):
    def __init__(self):
        super().__init__('RPC timeout')


class Forbidden(ApiException):
    def __init__(self, msg):
        super().__init__(f'Forbidden ({msg})')


def check_resp(func: Callable[..., httpx.Response]):
    @functools.wraps(func)
    async def wrapper(*args, **kwargs):
        resp: httpx.Response = await func(*args, **kwargs)
        if resp.status_code == httpx.codes.FORBIDDEN:
            raise Forbidden(resp.json())
        resp.raise_for_status()
        return resp

    return wrapper


class ApiNodeMixin:
    @property
    def http_client(self):
        return httpx.AsyncClient(base_url=self.base_url,
                                 headers=self.headers)

    @check_resp
    async def get(self, *args, **kwargs):
        async with self.http_client as c:
            return await c.get(*args, **kwargs)

    @check_resp
    async def post(self, *args, **kwargs):
        async with self.http_client as c:
            return await c.post(*args, **kwargs)

    @check_resp
    async def put(self, *args, **kwargs):
        async with self.http_client as c:
            return await c.put(*args, **kwargs)

    @check_resp
    async def delete(self, *args, **kwargs):
        async with self.http_client as c:
            return await c.delete(*args, **kwargs)


class Client(ApiNodeMixin):
    def __init__(self,
                 config_path: Optional[Union[Path, str]] = None,
                 api_key: Optional[str] = None):
        if not config_path:
            config_path = Path.home() / '.golioth' / '.goliothctl.yaml'
        elif isinstance(config_path, str):
            config_path = Path(config_path)

        self.config_path: Path = config_path
        self.load_config()

        url = self.config['apiurl']
        self.base_url: str = f'{url}/v1'

        self.headers: Dict[str, str] = {}

        if api_key is not None:
            self.headers['x-api-key'] = api_key
        elif 'accesstoken' in self.config and \
             (accesstoken := self.config['accesstoken']):
            self.headers['authorization'] = f'bearer {accesstoken}'

    def load_config(self):
        with self.config_path.open('r') as fp:
            self.config = yaml.load(fp, yaml.SafeLoader)

    async def get_projects(self) -> list[Project]:
        resp = await self.get('projects')
        return [Project(self, p) for p in resp.json()['list']]

    async def project_by_name(self, name: str) -> Project:
        for project in await self.get_projects():
            if project.name == name:
                return project

        raise ProjectNotFound(f'No project with name {name}')

    async def default_project(self) -> Project:
        project_id = self.config['projectid']

        return await Project.get_by_id(self, project_id)


class LogsMonitor:
    def __init__(self, ws: WebSocketConnection):
        self.ws = ws

    async def __anext__(self) -> LogEntry:
        return await self.get()

    async def get(self) -> LogEntry:
        msg = await self.ws.get_message()
        msg = json.loads(msg)

        if 'error' in msg:
            error = msg['error']
            # REVISIT: Not sure error code should map to all RPCStatusCode values, but at least it
            # matches PERMISSION_DENIED.
            if error['code'] == RPCStatusCode.PERMISSION_DENIED.value:
                raise Forbidden(error['message'])

            raise ApiException(f"code={error['code']} message={error['message']}")

        log = LogEntry(msg['result']['data'])
        return log


class Project(ApiNodeMixin):
    def __init__(self, client: Client, info: dict[str, Any]):
        self.client = client
        self.info: dict[str, Any] = info
        self.base_url: str = f'{client.base_url}/projects/{self.id}'
        self.artifacts: ProjectArtifacts = ProjectArtifacts(self)
        self.releases: ProjectReleases = ProjectReleases(self)
        self.certificates: ProjectCertificates = ProjectCertificates(self)
        self.settings: ProjectSettings = ProjectSettings(self)

    @property
    def headers(self) -> Dict[str, str]:
        return self.client.headers

    @staticmethod
    async def get_by_id(client: Client, project_id: str):
        resp = await client.get(f'projects/{project_id}')
        return Project(client, resp.json()['data'])

    @property
    def id(self) -> str:
        return self.info['id']

    @property
    def name(self) -> str:
        return self.info['name']

    async def get_devices(self, params: dict = {}) -> list[Device]:
        resp = await self.get('devices', params=params)
        return [Device(self, d) for d in resp.json()['list']]

    async def device_by_name(self, name: str) -> Device:
        devices = await self.get_devices({'deviceName': name})
        if not devices:
            raise DeviceNotFound(f'No device with name {name}')

        return devices[0]

    async def get_logs(self, params: dict = {}) -> list[LogEntry]:
        resp = await self.get('logs', params=params)
        return [LogEntry(e) for e in reversed(resp.json()['list'])]

    @asynccontextmanager
    async def logs_websocket(self, params: dict = {}) -> WebSocketConnection:
        async with open_websocket_url(f'{self.client.base_url.replace("http", "ws")}/ws/projects/{self.id}/logs',
                                      extra_headers=[(k, v) for k, v in self.headers.items()]) as ws:
            yield ws

    @asynccontextmanager
    async def logs_monitor(self, params: dict = {}) -> LogsMonitor:
        async with self.logs_websocket(params) as ws:
            yield LogsMonitor(ws)

    async def logs_iter(self, lines: int = 0, params: dict = {}) -> Iterable[LogEntry]:
        async with self.logs_monitor(params) as monitor:
            old_logs = []

            if lines != 0:
                old_logs = await self.get_logs(params)

                if lines > 0:
                    old_logs = old_logs[-lines:]

                for log in old_logs[-lines:]:
                    yield log

            # TODO: do not reprint logs from 'old_logs'

            while True:
                log = await monitor.get()

                if log in old_logs:
                    continue

                yield log


class LogLevel(Enum):
    NON = 'NONE'
    DBG = 'DEBUG'
    INF = 'INFO'
    WRN = 'WARN'
    ERR = 'ERROR'


class LogEntry:
    def __init__(self, info: dict[str, Any]):
        self.info = info

    def __repr__(self) -> str:
        return f'LogEntry <[{self.datetime}] <{self.level.name}> "{self.message}">'

    @property
    def device_id(self) -> str:
        return self.info['device_id']

    @property
    def level(self) -> LogLevel:
        return LogLevel(self.info['level'])

    @property
    def message(self) -> str:
        return self.info['message']

    @property
    def datetime(self) -> datetime:
        ts = re.sub('(\d{6})\d*Z$', '\g<1>+00:00', self.info['timestamp'])
        return datetime.fromisoformat(ts)

    @property
    def module(self) -> str:
        return self.info['module']

    @property
    def type(self) -> str:
        return self.info['type']

    @property
    def metadata(self) -> dict:
        return self.info['metadata']

class Device(ApiNodeMixin):
    def __init__(self, project: Project, info: dict[str, Any]):
        self.project = project
        self.info = info
        self.base_url = f'{project.base_url}/devices/{self.id}'
        self.rpc = DeviceRPC(self)
        self.lightdb = DeviceLightDB(self)

    @property
    def headers(self) -> Dict[str, str]:
        return self.project.headers

    @property
    def id(self):
        return self.info['id']

    @property
    def name(self):
        return self.info['name']

    @property
    def enabled(self):
        return self.info["enabled"]

    async def get_logs(self, params: dict = {}) -> list[LogEntry]:
        params['deviceId'] = self.id
        return await self.project.get_logs(params=params)

    @asynccontextmanager
    async def logs_monitor(self, params: dict = {}) -> LogsMonitor:
        params['deviceId'] = self.id
        async with self.project.logs_websocket(params=params) as ws:
            yield LogsMonitor(ws)

    async def logs_iter(self, lines: int = 0, params: dict = {}) -> Iterable[LogEntry]:
        params['deviceId'] = self.id
        async for log in self.project.logs_iter(lines=lines, params=params):
            yield log


class DeviceLightDB(ApiNodeMixin):
    ValueType = Union[str, int, float, bool, 'ValueType']

    def __init__(self, device: Device):
        self.device = device
        self.base_url: str = device.base_url

    @property
    def headers(self) -> Dict[str, str]:
        return self.device.headers

    async def get(self, path: str) -> DeviceLightDB.ValueType:
        async with self.http_client as c:
            response = await c.get(f'data/{path}')
            return response.json()['data']

    async def set(self, path: str, value: ValueType) -> None:
        async with self.http_client as c:
            await c.post(f'data/{path}', json=value)

    async def delete(self, path: str) -> None:
        async with self.http_client as c:
            await c.delete(f'data/{path}')


class DeviceRPC(ApiNodeMixin):
    def __init__(self, device: Device):
        self.device = device
        self.base_url: str = device.base_url

    @property
    def headers(self) -> Dict[str, str]:
        return self.device.headers

    async def call(self, method: str, params: Union[list, dict]):
        async with self.http_client as c:
            try:
                response = await c.post('rpc', json={
                    "method": method,
                    "params": params,
                })
            except httpx.ReadTimeout as e:
                raise RPCTimeout() from e

            json_response = response.json()

            if json_response['statusCode'] == 0:
                return json_response['detail']

            raise RPCResultError(json_response['statusCode'])

    def __getattr__(self, name):
        async def call_method(*args, **kwargs):
            if args:
                params = args
            else:
                params = kwargs

            return await self.call(name, params)

        return call_method


class Certificate(ApiNodeMixin):
    def __init__(self, project: Project, info: dict[str, Any]):
        self.project = project
        self.info = info
        self.base_url = f'{project.base_url}/certificates/{self.id}'

    @property
    def headers(self) -> Dict[str, str]:
        return self.project.headers

    @property
    def id(self):
        return self.info['id']

    @property
    def enabled(self):
        return self.info['enabled']

    def __repr__(self):
        return f'Certificate <{self.id}, enabled={self.enabled}>'


class Artifact(ApiNodeMixin):
    class Error(ApiException):
        pass

    class InUse(Error):
        pass

    class AlreadyExists(Error):
        pass

    def __init__(self, project: Project, info: dict[str, Any]):
        self.project = project
        self.info = info
        self.base_url = f'{project.base_url}/artifacts/{self.id}'

    @property
    def headers(self) -> Dict[str, str]:
        return self.project.headers

    @property
    def id(self):
        return self.info['id']

    @property
    def package(self):
        return self.info['package']

    @property
    def version(self):
        return self.info['version']

    def __repr__(self):
        return f'Artifact <{self.id}, package={self.package}, version={self.version}>'


class Release(ApiNodeMixin):
    class Error(ApiException):
        pass

    class TagInUse(Error):
        pass

    def __init__(self, project: Project, info: dict[str, Any]):
        self.project = project
        self.info = info
        self.base_url = f'{project.base_url}/releases/{self.id}'

    @property
    def headers(self) -> Dict[str, str]:
        return self.project.headers

    @property
    def id(self):
        return self.info['id']

    @property
    def tags(self):
        return self.info['releaseTags']

    @property
    def artifact_ids(self):
        return self.info['artifactIds']

    @property
    def rollout(self):
        return self.info['rollout']

    def __repr__(self):
        return f'Release <{self.id}, tags={self.tags}, artifact_ids={self.artifact_ids}, rollout={self.rollout}>'


class ProjectArtifacts(ApiNodeMixin):
    def __init__(self, project: Project):
        self.project = project
        self.base_url = self.project.base_url + '/artifacts'

    @property
    def headers(self) -> Dict[str, str]:
        return self.project.headers

    async def get_all(self) -> list:
        resp = await self.project.get('artifacts')
        return [Artifact(self, d) for d in resp.json()['list']]

    async def get(self, artifact_id: str):
        resp = await super().get(artifact_id)
        return Artifact(self.project, resp.json()['data'])

    async def delete(self, artifact_id: str):
        try:
            return await super().delete(artifact_id)
        except httpx.HTTPStatusError as err:
            msg = err.response.json()['message']
            if 'artifact in use' in msg:
                raise Artifact.InUse(f'Artifact {artifact_id} in use') from err
            elif 'hex string is not a valid ObjectID' in msg:
                raise InvalidObjectID(msg) from err

            raise err

    async def upload(self,
                     path: Path,
                     version: str,
                     package: str = 'main') -> Artifact:
        json = {
            'projectId': self.project.id,
            'content': b64encode(path.open('rb').read()).decode(),
            'version': version,
            'package': package,
        }

        try:
            response = await self.project.client.post('artifacts', json=json)
        except httpx.HTTPStatusError as err:
            msg = err.response.json()['message']
            if 'artifact already exists' in msg:
                raise Artifact.AlreadyExists(msg) from err

            raise err

        return Artifact(self.project, response.json()['data'])


class ProjectReleases(ApiNodeMixin):
    def __init__(self, project: Project):
        self.project = project
        self.base_url = self.project.base_url + '/releases'

    @property
    def headers(self) -> Dict[str, str]:
        return self.project.headers

    async def get_all(self) -> list:
        resp = await self.project.get('releases')
        return [Release(self, d) for d in resp.json()['list']]

    async def get(self, release_id: str):
        resp = await super().get(release_id)
        return Release(self.project, resp.json()['data'])

    async def delete(self, release_id: str):
        try:
            return await super().delete(release_id)
        except httpx.HTTPStatusError as err:
            msg = err.response.json()['message']
            if 'hex string is not a valid ObjectID' in msg:
                raise InvalidObjectID from err

            raise err

    async def create(self,
                     artifact_ids: list[str],
                     tags: list[str],
                     rollout: bool = False):
        json = {
            'releaseTags': tags,
            'artifactIds': artifact_ids,
            'rollout': rollout,
        }

        try:
            response = await self.project.post('releases', json=json)
        except httpx.HTTPStatusError as err:
            msg = err.response.json()['message']
            if 'already exists with same release tag' in msg:
                raise Release.TagInUse(msg) from err

            raise err

        return Release(self.project, response.json()['data'])


class ProjectCertificates(ApiNodeMixin):
    def __init__(self, project: Project):
        self.project = project
        self.base_url = self.project.base_url

    @property
    def headers(self) -> Dict[str, str]:
        return self.project.headers

    async def get_all(self) -> list:
        resp = await self.project.get('certificates')
        return [Certificate(self, d) for d in resp.json()['list']]

    async def get(self, cert_id: str):
        resp = await self.project.get(f'certificates/{cert_id}')
        return Certificate(self.project, resp.json()['data'])

    async def add(self, cert_pem: bytes, cert_type: Literal['root', 'intermediate']):
        json = {
            'certFile': b64encode(cert_pem).decode(),
            'certType': cert_type,
        }

        response = await self.project.post('certificates', json=json)

        return response.json()

    async def delete(self, cert_id: str):
        response = await self.project.delete(f'certificates/{cert_id}')
        return response.json()


class ProjectSettings:
    def __init__(self, project: Project):
        self.project = project

    async def get_all(self) -> list:
        response = await self.project.get('settings')
        return response.json()['list']

    async def get(self, key: str):
        settings = await self.get_all()
        for setting in settings:
            if setting['key'] == key:
                return setting

        raise KeyError(f"No setting with {key=}")

    async def set(self, key: str, value: Union[int, float, bool, str],
                  override: bool = True):
        if isinstance(value, int):
            data_type = 'integer'
        elif isinstance(value, float):
            data_type = 'float'
        elif isinstance(value, bool):
            data_type = 'boolean'
        elif isinstance(value, str):
            data_type = 'string'
        else:
            raise RuntimeError("Invalid value type")

        json = {
            "key": key,
            "dataType": data_type,
            "value": value,
        }

        if override:
            try:
                setting = await self.get(key)
                response = await self.project.put('settings/' + setting['id'], json=json)

                return response.json()
            except KeyError:
                pass

        response = await self.project.post('settings', json=json)

        return response.json()

    async def delete(self, key: str):
        try:
            setting = await self.get(key)
            await self.project.delete('settings/' + setting['id'])
        except KeyError:
            pass
