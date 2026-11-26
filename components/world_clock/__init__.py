import esphome.codegen as cg
import esphome.config_validation as cv

CODEOWNERS = ["@smelrose"]

CONF_STANDALONE = "standalone"
CONF_CALENDAR_ICS = "calendar_ics"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_STANDALONE, default=False): cv.boolean,
        cv.Optional(CONF_CALENDAR_ICS, default=False): cv.boolean,
    }
)


async def to_code(config):
    if config[CONF_STANDALONE] or config[CONF_CALENDAR_ICS]:
        cg.add_define("USE_WORLD_CLOCK_NET_WORKER")
    if config[CONF_STANDALONE]:
        cg.add_define("USE_WORLD_CLOCK_STANDALONE_POLL")
