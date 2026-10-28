# Writes OUTPUT, a C++ file holding every guide in GUIDES_DIR (<id>.md) as a string, so the
# engine ships them without files of its own to find.
file(GLOB guides "${GUIDES_DIR}/*.md")
list(SORT guides)
set(entries "")

foreach(guide IN LISTS guides)
    get_filename_component(id "${guide}" NAME_WE)
    file(READ "${guide}" text)
    string(APPEND entries "        { \"${id}\", R\"gkguide(${text})gkguide\" },\n")
endforeach()

file(WRITE "${OUTPUT}.tmp" "// Generated from guides/*.md by engine/cmake/EmbedGuides.cmake. Don't edit.\n\n#include \"guides/Guides.h\"\n\nnamespace gk::guides\n{\n\nconst std::vector<std::pair<std::string, std::string>>& builtInTexts()\n{\n    static const std::vector<std::pair<std::string, std::string>> texts {\n${entries}    };\n    return texts;\n}\n\n} // namespace gk::guides\n")
file(COPY_FILE "${OUTPUT}.tmp" "${OUTPUT}" ONLY_IF_DIFFERENT)
