import esphome.codegen as cg
from esphome.components import number
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_INITIAL_VALUE

from . import CONF_JC_ID, JC3248Media, jc3248_media_ns

DEPENDENCIES = ["jc3248_media"]

JC3248BrightnessNumber = jc3248_media_ns.class_(
    "JC3248BrightnessNumber",
    number.Number,
    cg.Component,
    cg.Parented.template(JC3248Media),
)

CONFIG_SCHEMA = number.number_schema(JC3248BrightnessNumber).extend(
    {
        cv.Required(CONF_JC_ID): cv.use_id(JC3248Media),
        cv.Optional(CONF_INITIAL_VALUE, default=100.0): cv.float_range(min=0, max=100),
    }
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await number.register_number(var, config, min_value=0.0, max_value=100.0, step=1.0)
    cg.add(var.set_initial_value(config[CONF_INITIAL_VALUE]))
    parent = await cg.get_variable(config[CONF_JC_ID])
    cg.add(var.set_parent(parent))
