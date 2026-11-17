import esphome.codegen as cg
from esphome.components import select
import esphome.config_validation as cv
from esphome.const import CONF_ID

from . import CONF_JC_ID, JC3248Media, jc3248_media_ns

DEPENDENCIES = ["jc3248_media"]

JC3248Select = jc3248_media_ns.class_(
    "JC3248Select",
    select.Select,
    cg.Component,
    cg.Parented.template(JC3248Media),
)

CONF_PLAY_ON_SELECT = "play_on_select"

CONFIG_SCHEMA = select.select_schema(JC3248Select).extend(
    {
        cv.Required(CONF_JC_ID): cv.use_id(JC3248Media),
        # A second selector can be used to pick a file for deletion without
        # starting playback.
        cv.Optional(CONF_PLAY_ON_SELECT, default=True): cv.boolean,
    }
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    # Options are filled from the SD card at runtime, so no static list.
    await select.register_select(var, config, options=[])
    parent = await cg.get_variable(config[CONF_JC_ID])
    cg.add(var.set_parent(parent))
    cg.add(var.set_play_on_select(config[CONF_PLAY_ON_SELECT]))
