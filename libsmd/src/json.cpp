#include "smd/json.hpp"
#include "text.hpp"
#include <charconv>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace smd {
namespace {
class Writer {
public:
    std::string out;
    void string(const std::string& text) {
        detail::validateUtf8(text);
        static constexpr char hex[] = "0123456789abcdef";
        out += '"';
        for (const unsigned char c : text) {
            if (c == '"' || c == '\\') { out += '\\'; out += static_cast<char>(c); }
            else if (c < 32) {
                out += "\\u00";
                out += hex[c >> 4]; out += hex[c & 15];
            } else out += static_cast<char>(c);
        }
        out += '"';
    }
    void number(double value) {
        if (!std::isfinite(value)) { out += "null"; return; }
        if (value == 0) { out += '0'; return; } // JSON.stringify normalizes -0.
        char buffer[64];
        const auto result = std::to_chars(buffer,buffer+sizeof(buffer),value,
            std::chars_format::general,std::numeric_limits<double>::max_digits10);
        if (result.ec != std::errc{}) throw std::runtime_error("JSON number conversion failed.");
        out.append(buffer,result.ptr);
    }
    template<class T, class F> void array(const T& values, F write) {
        out += '[';
        bool first = true;
        for (const auto& value : values) {
            if (!first) out += ',';
            first = false;
            write(value);
        }
        out += ']';
    }
    void vec(Vec2 v) { out += "{\"x\":"; number(v.x); out += ",\"y\":"; number(v.y); out += '}'; }
    void vec(Vec3 v) { out += "{\"x\":"; number(v.x); out += ",\"y\":"; number(v.y); out += ",\"z\":"; number(v.z); out += '}'; }
    void plane(Plane p) {
        out += "{\"a\":"; number(p.a); out += ",\"b\":"; number(p.b);
        out += ",\"c\":"; number(p.c); out += ",\"d\":"; number(p.d); out += '}';
    }
    void brep(const ParsedBrep& b) {
        out += "{\"verticesLocal\":"; array(b.verticesLocal,[&](auto v){vec(v);});
        out += ",\"verticesWorld\":"; array(b.verticesWorld,[&](auto v){vec(v);});
        out += ",\"faces\":"; array(b.faces,[&](const Face& f){
            out += "{\"flag\":"; number(f.flag);
            out += ",\"indices\":"; array(f.indices,[&](auto i){out += std::to_string(i);});
            if (f.plane) { out += ",\"plane\":"; plane(*f.plane); }
            if (f.name) { out += ",\"name\":"; string(*f.name); }
            out += '}';
        });
        out += ",\"planes\":"; array(b.planes,[&](auto p){plane(p);});
        out += ",\"faceNames\":"; array(b.faceNames,[&](const auto& s){string(s);});
        if (b.transform) {
            out += ",\"transform\":{\"values\":";
            array(b.transform->values,[&](auto d){number(d);}); out += '}';
        }
        out += ",\"vertexCountTokenIndex\":" + std::to_string(b.vertexCountTokenIndex) + '}';
    }
};
}
std::string toJsonV1(const ParsedSmd& p) {
    Writer w;
    w.out = "{\"schemaVersion\":1,\"parsed\":{\"isDicadAsc\":";
    w.out += p.isDicadAsc ? "true" : "false";
    w.out += ",\"headerLines\":"; w.array(p.headerLines,[&](const auto& s){w.string(s);});
    w.out += ",\"geometryBlockLines\":"; w.array(p.geometryBlockLines,[&](const auto& s){w.string(s);});
    w.out += ",\"geometry\":{";
    const auto& g = p.geometry;
    if (g.objectId) { w.out += "\"objectId\":"; w.string(*g.objectId); w.out += ','; }
    if (g.name) { w.out += "\"name\":"; w.string(*g.name); w.out += ','; }
    if (g.partType) { w.out += "\"partType\":"; w.number(*g.partType); w.out += ','; }
    if (g.placementOrigin) { w.out += "\"placementOrigin\":"; w.vec(*g.placementOrigin); w.out += ','; }
    if (g.xAxis) { w.out += "\"xAxis\":"; w.vec(*g.xAxis); w.out += ','; }
    if (g.yAxis) { w.out += "\"yAxis\":"; w.vec(*g.yAxis); w.out += ','; }
    w.out += "\"profileEdges\":";
    w.array(g.profileEdges,[&](const ProfileEdge& e){
        w.out += "{\"id\":"; w.string(e.id); w.out += ",\"index\":"; w.number(e.index);
        w.out += ",\"type\":"; w.number(e.type); w.out += ",\"start\":"; w.vec(e.start);
        w.out += ",\"end\":"; w.vec(e.end); w.out += '}';
    });
    w.out += ",\"brep\":"; w.brep(g.brep); w.out += "}}}";
    return w.out;
}
std::string tokensToJsonV1(std::span<const Token> tokens) {
    Writer w;
    w.out = "{\"schemaVersion\":1,\"tokens\":";
    w.array(tokens,[&](const Token& t){
        w.out += "{\"type\":";
        w.string(t.type == TokenType::number ? "number" : t.type == TokenType::block ? "block" : "string");
        w.out += ",\"value\":";
        if (t.type == TokenType::number) w.number(std::get<double>(t.value));
        else w.string(std::get<std::string>(t.value));
        w.out += ",\"raw\":"; w.string(t.raw);
        w.out += ",\"line\":" + std::to_string(t.line) + '}';
    });
    w.out += '}';
    return w.out;
}
} // namespace smd
