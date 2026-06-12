# interfaces — header-only component, no IDF dependencies.
# These headers must stay includable on the host (linux preview target)
# without pulling in any ESP-IDF or hardware headers.
idf_component_register(
    INCLUDE_DIRS "include"
)
