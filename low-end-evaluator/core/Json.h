#pragma once

// Minimal JSON value, writer and parser (no JUCE): enough for profiles and reports. Objects keep insertion order so files
// diff cleanly; numbers are written with 10 significant digits so a profile survives a round trip.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace lee
{
struct Json
{
    enum Type { Null, Bool, Num, Str, Arr, Obj };
    Type t = Null;
    bool b = false;
    double n = 0.0;
    std::string s;
    std::vector<Json> a;
    std::vector<std::pair<std::string, Json>> o;

    static Json num (double v) { Json j; j.t = Num; j.n = v; return j; }
    static Json str (std::string v) { Json j; j.t = Str; j.s = std::move (v); return j; }
    static Json boolean (bool v) { Json j; j.t = Bool; j.b = v; return j; }
    static Json array() { Json j; j.t = Arr; return j; }
    static Json object() { Json j; j.t = Obj; return j; }

    static Json numbers (const std::vector<double>& v)
    {
        Json j = array();
        for (double x : v)
            j.a.push_back (num (x));
        return j;
    }

    const Json* get (const std::string& k) const
    {
        for (auto& kv : o)
            if (kv.first == k)
                return &kv.second;
        return nullptr;
    }
    Json& set (const std::string& k, Json v)
    {
        for (auto& kv : o)
            if (kv.first == k)
                return kv.second = std::move (v);
        o.emplace_back (k, std::move (v));
        return o.back().second;
    }
    Json& push (Json v)
    {
        a.push_back (std::move (v));
        return a.back();
    }

    double numOr (const std::string& k, double d) const
    {
        auto* j = get (k);
        return j && j->t == Num ? j->n : d;
    }
    std::string strOr (const std::string& k, const std::string& d) const
    {
        auto* j = get (k);
        return j && j->t == Str ? j->s : d;
    }
    bool boolOr (const std::string& k, bool d) const
    {
        auto* j = get (k);
        return j && j->t == Bool ? j->b : d;
    }
    std::vector<double> numsOr (const std::string& k) const
    {
        std::vector<double> v;
        if (auto* j = get (k); j && j->t == Arr)
            for (auto& e : j->a)
                v.push_back (e.t == Num ? e.n : NAN);
        return v;
    }
};

namespace detail
{
inline void escape (std::string& out, const std::string& s)
{
    out += '"';
    for (unsigned char c : s)
    {
        if (c == '"' || c == '\\')
            (out += '\\') += (char) c;
        else if (c == '\n')
            out += "\\n";
        else if (c == '\t')
            out += "\\t";
        else if (c < 0x20)
        {
            char buf[8];
            std::snprintf (buf, sizeof buf, "\\u%04x", c);
            out += buf;
        }
        else
            out += (char) c;
    }
    out += '"';
}

// arrays of plain numbers stay on one line; everything else is indented
inline void dump (std::string& out, const Json& j, int depth)
{
    auto nl = [&] (int d) { out += '\n'; out.append ((size_t) d * 2, ' '); };
    switch (j.t)
    {
        case Json::Null: out += "null"; break;
        case Json::Bool: out += j.b ? "true" : "false"; break;
        case Json::Num:
        {
            if (! std::isfinite (j.n))
            {
                out += "null";
                break;
            }
            char buf[32];
            std::snprintf (buf, sizeof buf, "%.10g", j.n);
            out += buf;
            break;
        }
        case Json::Str: escape (out, j.s); break;
        case Json::Arr:
        {
            bool flat = true;
            for (auto& e : j.a)
                flat = flat && (e.t == Json::Num || e.t == Json::Null || e.t == Json::Bool);
            out += '[';
            for (size_t i = 0; i < j.a.size(); ++i)
            {
                if (i)
                    out += flat ? ", " : ",";
                if (! flat)
                    nl (depth + 1);
                dump (out, j.a[i], depth + 1);
            }
            if (! flat && ! j.a.empty())
                nl (depth);
            out += ']';
            break;
        }
        case Json::Obj:
        {
            out += '{';
            for (size_t i = 0; i < j.o.size(); ++i)
            {
                if (i)
                    out += ',';
                nl (depth + 1);
                escape (out, j.o[i].first);
                out += ": ";
                dump (out, j.o[i].second, depth + 1);
            }
            if (! j.o.empty())
                nl (depth);
            out += '}';
            break;
        }
    }
}

struct Parser
{
    const std::string& s;
    size_t i = 0;
    std::string err;

    void ws()
    {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t'))
            ++i;
    }
    bool fail (const char* what)
    {
        if (err.empty())
            err = std::string (what) + " at offset " + std::to_string (i);
        return false;
    }
    bool lit (const char* w)
    {
        size_t k = 0;
        for (; w[k]; ++k)
            if (i + k >= s.size() || s[i + k] != w[k])
                return false;
        i += k;
        return true;
    }
    bool str (std::string& out)
    {
        if (s[i] != '"')
            return fail ("expected string");
        ++i;
        while (i < s.size() && s[i] != '"')
        {
            char c = s[i++];
            if (c == '\\')
            {
                if (i >= s.size())
                    return fail ("bad escape");
                c = s[i++];
                switch (c)
                {
                    case 'n': out += '\n'; break;
                    case 't': out += '\t'; break;
                    case 'r': out += '\r'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'u':
                    {
                        if (i + 4 > s.size())
                            return fail ("bad \\u escape");
                        const unsigned cp = (unsigned) std::strtoul (s.substr (i, 4).c_str(), nullptr, 16);
                        i += 4;
                        // UTF-8 encode (BMP only; surrogate pairs are passed through as two code points)
                        if (cp < 0x80)
                            out += (char) cp;
                        else if (cp < 0x800)
                        {
                            out += (char) (0xC0 | (cp >> 6));
                            out += (char) (0x80 | (cp & 0x3F));
                        }
                        else
                        {
                            out += (char) (0xE0 | (cp >> 12));
                            out += (char) (0x80 | ((cp >> 6) & 0x3F));
                            out += (char) (0x80 | (cp & 0x3F));
                        }
                        break;
                    }
                    default: out += c;
                }
            }
            else
                out += c;
        }
        if (i >= s.size())
            return fail ("unterminated string");
        ++i;
        return true;
    }
    bool value (Json& j, int depth)
    {
        if (depth > 64)
            return fail ("nesting too deep");
        ws();
        if (i >= s.size())
            return fail ("unexpected end");
        const char c = s[i];
        if (c == '{')
        {
            j = Json::object();
            ++i;
            ws();
            if (i < s.size() && s[i] == '}')
                return ++i, true;
            for (;;)
            {
                ws();
                std::string k;
                if (i >= s.size() || ! str (k))
                    return fail ("expected key");
                ws();
                if (i >= s.size() || s[i] != ':')
                    return fail ("expected ':'");
                ++i;
                Json v;
                if (! value (v, depth + 1))
                    return false;
                j.o.emplace_back (std::move (k), std::move (v));
                ws();
                if (i < s.size() && s[i] == ',')
                {
                    ++i;
                    continue;
                }
                if (i < s.size() && s[i] == '}')
                    return ++i, true;
                return fail ("expected ',' or '}'");
            }
        }
        if (c == '[')
        {
            j = Json::array();
            ++i;
            ws();
            if (i < s.size() && s[i] == ']')
                return ++i, true;
            for (;;)
            {
                Json v;
                if (! value (v, depth + 1))
                    return false;
                j.a.push_back (std::move (v));
                ws();
                if (i < s.size() && s[i] == ',')
                {
                    ++i;
                    continue;
                }
                if (i < s.size() && s[i] == ']')
                    return ++i, true;
                return fail ("expected ',' or ']'");
            }
        }
        if (c == '"')
        {
            j = Json::str ("");
            return str (j.s);
        }
        if (lit ("true"))
            return j = Json::boolean (true), true;
        if (lit ("false"))
            return j = Json::boolean (false), true;
        if (lit ("null"))
            return j = Json(), true;
        const char* p = s.c_str() + i;
        char* e = nullptr;
        const double v = std::strtod (p, &e);
        if (e == p)
            return fail ("unexpected character");
        i += (size_t) (e - p);
        j = Json::num (v);
        return true;
    }
};
} // namespace detail

inline std::string dump (const Json& j)
{
    std::string out;
    detail::dump (out, j, 0);
    out += '\n';
    return out;
}

inline bool parse (const std::string& text, Json& out, std::string& err)
{
    detail::Parser p { text, 0, {} };
    if (! p.value (out, 0))
    {
        err = p.err;
        return false;
    }
    p.ws();
    if (p.i != text.size())
    {
        err = "trailing characters at offset " + std::to_string (p.i);
        return false;
    }
    return true;
}
} // namespace lee
