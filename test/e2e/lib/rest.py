"""REST client for the can-hub-web API, with OpenAPI schema validation.

A thin `requests.Session` wrapper that carries the session cookie and the CSRF
token automatically, and validates response bodies against the schemas in
`web/openapi.yaml` — so the e2e suite asserts the wire shape against the
contract, not against a hand-copy of it.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import requests
import yaml
from jsonschema import Draft202012Validator
from referencing import Registry, Resource
from referencing.jsonschema import DRAFT202012

DEFAULT_SPEC = "/work/web/openapi.yaml"


@dataclass
class ApiResult:
    status: int
    json: object
    text: str


class SchemaValidator:
    def __init__(self, spec_path: str = DEFAULT_SPEC):
        spec = yaml.safe_load(Path(spec_path).read_text())
        resource = Resource(contents=spec, specification=DRAFT202012)
        self._registry = Registry().with_resource("urn:openapi", resource)

    def validate(self, schema_name: str, instance: object) -> None:
        schema = {"$ref": f"urn:openapi#/components/schemas/{schema_name}"}
        Draft202012Validator(schema, registry=self._registry).validate(instance)

    def validate_each(self, schema_name: str, items) -> None:
        for item in items:
            self.validate(schema_name, item)


class RestClient:
    def __init__(self, base_url: str, spec_path: str = DEFAULT_SPEC):
        self.base_url = base_url.rstrip("/")
        self.session = requests.Session()
        self.csrf: str | None = None
        self.schema = SchemaValidator(spec_path)

    # ---------- transport ----------

    def healthz_ok(self) -> bool:
        response = self.session.get(self.base_url + "/healthz", timeout=1)
        return response.ok and response.text.strip() == "ok"

    def get(self, path: str) -> ApiResult:
        return self._result(self.session.get(self.base_url + path, timeout=5))

    def post(self, path: str, body: object | None = None) -> ApiResult:
        return self._mutate("POST", path, body)

    def put(self, path: str, body: object | None = None) -> ApiResult:
        return self._mutate("PUT", path, body)

    def delete(self, path: str, body: object | None = None) -> ApiResult:
        return self._mutate("DELETE", path, body)

    # ---------- auth shortcuts ----------

    def setup(self, name: str, password: str) -> ApiResult:
        return self.post("/api/setup", {"name": name, "password": password})

    def login(self, name: str, password: str) -> ApiResult:
        return self.post("/api/login", {"name": name, "password": password})

    def logout(self) -> ApiResult:
        result = self.post("/api/logout")
        self.csrf = None
        return result

    def auth_state(self) -> ApiResult:
        return self.get("/api/auth/state")

    def clear_csrf(self) -> None:
        self.csrf = None

    # ---------- internals ----------

    def _mutate(self, method: str, path: str, body: object | None) -> ApiResult:
        headers = {}
        if self.csrf:
            headers["X-CSRF-Token"] = self.csrf
        response = self.session.request(
            method, self.base_url + path, json=body, headers=headers, timeout=5
        )
        return self._result(response)

    def _result(self, response: requests.Response) -> ApiResult:
        try:
            body = response.json()
        except ValueError:
            body = None
        if isinstance(body, dict) and body.get("csrfToken"):
            self.csrf = body["csrfToken"]
        return ApiResult(response.status_code, body, response.text)
