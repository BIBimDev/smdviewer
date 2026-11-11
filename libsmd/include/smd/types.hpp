#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace smd {
enum class TokenType { number, string, block };
struct Token {
    TokenType type;
    std::variant<double, std::string> value;
    std::string raw;
    std::size_t line;
};
struct Vec2 { double x, y; };
struct Vec3 { double x, y, z; };
struct ProfileEdge {
    std::string id;
    // TS Number, deliberately not narrowed to a machine integer.
    double index, type;
    Vec2 start, end;
};
struct Plane { double a, b, c, d; };
struct Face {
    double flag;
    std::vector<std::size_t> indices;
    std::optional<Plane> plane;
    std::optional<std::string> name;
};
struct Transform3x4 { std::array<double, 12> values; };
struct ParsedBrep {
    std::vector<Vec3> verticesLocal, verticesWorld;
    std::vector<Face> faces;
    std::vector<Plane> planes;
    std::vector<std::string> faceNames;
    std::optional<Transform3x4> transform;
    std::size_t vertexCountTokenIndex;
};
struct Smd10125Geometry {
    std::optional<std::string> objectId, name;
    std::optional<double> partType;
    std::vector<ProfileEdge> profileEdges;
    std::optional<Vec3> placementOrigin, xAxis, yAxis;
    ParsedBrep brep;
};
struct ParsedSmd {
    bool isDicadAsc;
    std::vector<std::string> headerLines, geometryBlockLines;
    Smd10125Geometry geometry;
};
} // namespace smd
