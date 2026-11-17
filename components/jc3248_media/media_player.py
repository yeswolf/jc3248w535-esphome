import esphome.codegen as cg
from esphome.components import media_player
import esphome.config_validation as cv
from esphome.const import CONF_ID

from . import CONF_JC_ID, JC3248Media, jc3248_media_ns

DEPENDENCIES = ["jc3248_media"]

JC3248MediaPlayer = jc3248_media_ns.class_(
    "JC3248MediaPlayer",
    media_player.MediaPlayer,
    cg.Component,
    cg.Parented.template(JC3248Media),
)

CONFIG_SCHEMA = media_player.media_player_schema(JC3248MediaPlayer).extend(
    {
        cv.Required(CONF_JC_ID): cv.use_id(JC3248Media),
    }
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await media_player.register_media_player(var, config)
    parent = await cg.get_variable(config[CONF_JC_ID])
    cg.add(var.set_parent(parent))
