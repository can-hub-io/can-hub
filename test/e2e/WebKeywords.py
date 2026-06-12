"""Robot keyword layer over the can-hub-web REST client.

Keywords drive the daemon's REST API and assert response bodies against the
OpenAPI schemas, so the suite reads like:

    ${web}=     Start CAN HUB Web On ${LOCAL_SERVER} Against ${hub}
    ${r}=       Bootstrap Admin ${web}    admin    hunter2pass
    Status Of ${r} Should Be    200
    Body Of ${r} Should Match    AuthState
"""

from robot.api.deco import keyword, library

from lib import CanHubWeb, RestClient, SchemaValidator


@library(scope="GLOBAL")
class WebKeywords:
    def __init__(self):
        self._schema = None

    def _validator(self) -> SchemaValidator:
        if self._schema is None:
            self._schema = SchemaValidator()
        return self._schema

    @keyword("Reset Hub State On ${server}")
    def reset_hub_state(self, server):
        server.exec("rm", "-rf", str(server.work_dir / "hub-state"), check=False)

    @keyword("Start CAN HUB Web On ${server} Against ${hub}")
    def start_web(self, server, hub, port=8080) -> CanHubWeb:
        return CanHubWeb.start(server, hub, int(port))

    @keyword("Stop CAN HUB Web ${web}")
    def stop_web(self, web: CanHubWeb):
        web.stop()

    @keyword("New Unauthenticated Client For ${web}")
    def new_client(self, web: CanHubWeb) -> RestClient:
        return web.new_unauthenticated_client()

    @keyword("Bootstrap Admin ${target}")
    def bootstrap_admin(self, target, name, password):
        return self._rest(target).setup(name, password)

    @keyword("Login ${target} As")
    def login(self, target, name, password):
        return self._rest(target).login(name, password)

    @keyword("Logout ${target}")
    def logout(self, target):
        return self._rest(target).logout()

    @keyword("Auth State Of ${target}")
    def auth_state(self, target):
        return self._rest(target).auth_state()

    @keyword("Clear CSRF On ${target}")
    def clear_csrf(self, target):
        self._rest(target).clear_csrf()

    @keyword("GET ${path} On ${target}")
    def get(self, path, target):
        return self._rest(target).get(path)

    @keyword("POST ${path} On ${target} With ${body}")
    def post_with(self, path, target, body):
        return self._rest(target).post(path, body)

    @keyword("POST ${path} On ${target}")
    def post(self, path, target):
        return self._rest(target).post(path)

    @keyword("PUT ${path} On ${target} With ${body}")
    def put_with(self, path, target, body):
        return self._rest(target).put(path, body)

    @keyword("DELETE ${path} On ${target}")
    def delete(self, path, target):
        return self._rest(target).delete(path)

    @keyword("Status Of ${result} Should Be")
    def status_should_be(self, result, code):
        if result.status != int(code):
            raise AssertionError(f"expected status {code}, got {result.status}: {result.text}")

    @keyword("Body Of ${result} Should Match")
    def body_should_match(self, result, schema):
        self._validator().validate(schema, result.json)

    @keyword("Each Item Of ${result} Should Match")
    def each_item_should_match(self, result, schema):
        self._validator().validate_each(schema, result.json)

    @staticmethod
    def _rest(target) -> RestClient:
        return target.rest if isinstance(target, CanHubWeb) else target
