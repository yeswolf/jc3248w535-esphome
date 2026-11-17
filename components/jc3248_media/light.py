import esphome.codegen as cg
from esphome.components import light
import esphome.config_validation as cv
from esphome.const import CONF_OUTPUT_ID

from . import CONF_JC_ID, JC3248Media, jc3248_media_ns

DEPENDENCIES = ["jc3248_media"]

JC3248ScreenColor = jc3248_media_ns.class_(
    "JC3248ScreenColor",
    light.LightOutput,
    cg.Parented.template(JC3248Media),
)

CONFIG_SCHEMA = light.RGB_LIGHT_SCHEMA.extend(
    {
        cv.GenerateID(CONF_OUTPUT_ID): cv.declare_id(JC3248ScreenColor),
        cv.Required(CONF_JC_ID): cv.use_id(JC3248Media),
    }
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_OUTPUT_ID])
    await light.register_light(var, config)
    parent = await cg.get_variable(config[CONF_JC_ID])
    cg.add(var.set_parent(parent))
