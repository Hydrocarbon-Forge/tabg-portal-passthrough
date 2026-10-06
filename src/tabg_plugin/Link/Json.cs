using System;
using System.Collections;
using System.Collections.Generic;
using System.Globalization;
using System.Text;

namespace TabgPortal.Link
{
    // Tiny dependency-free JSON reader/writer. The link protocol (src/link/protocol.md) is all
    // small JSON text frames, so we don't need Newtonsoft - and avoiding it keeps the plugin's
    // dependency surface minimal (important inside an EAC-shimmed, local-only client).
    //
    // Parser returns:  Dictionary<string,object>, List<object>, string, double, bool, null.
    // Writer accepts the same shapes plus int/long/float.
    public static class Json
    {
        // ---------- writing ----------
        public static string Write(object value)
        {
            var sb = new StringBuilder(256);
            WriteValue(sb, value);
            return sb.ToString();
        }

        private static void WriteValue(StringBuilder sb, object v)
        {
            switch (v)
            {
                case null: sb.Append("null"); break;
                case bool b: sb.Append(b ? "true" : "false"); break;
                case string s: WriteString(sb, s); break;
                case float f: sb.Append(F(f)); break;
                case double d: sb.Append(F(d)); break;
                case int i: sb.Append(i.ToString(CultureInfo.InvariantCulture)); break;
                case long l: sb.Append(l.ToString(CultureInfo.InvariantCulture)); break;
                case IDictionary<string, object> map: WriteObject(sb, map); break;
                case IEnumerable arr: WriteArray(sb, arr); break;
                default: WriteString(sb, v.ToString()); break;
            }
        }

        private static string F(double d)
        {
            if (double.IsNaN(d) || double.IsInfinity(d)) return "0";
            return d.ToString("R", CultureInfo.InvariantCulture);
        }

        private static void WriteObject(StringBuilder sb, IDictionary<string, object> map)
        {
            sb.Append('{');
            bool first = true;
            foreach (var kv in map)
            {
                if (!first) sb.Append(',');
                first = false;
                WriteString(sb, kv.Key);
                sb.Append(':');
                WriteValue(sb, kv.Value);
            }
            sb.Append('}');
        }

        private static void WriteArray(StringBuilder sb, IEnumerable arr)
        {
            sb.Append('[');
            bool first = true;
            foreach (var item in arr)
            {
                if (!first) sb.Append(',');
                first = false;
                WriteValue(sb, item);
            }
            sb.Append(']');
        }

        private static void WriteString(StringBuilder sb, string s)
        {
            sb.Append('"');
            foreach (char c in s)
            {
                switch (c)
                {
                    case '"': sb.Append("\\\""); break;
                    case '\\': sb.Append("\\\\"); break;
                    case '\b': sb.Append("\\b"); break;
                    case '\f': sb.Append("\\f"); break;
                    case '\n': sb.Append("\\n"); break;
                    case '\r': sb.Append("\\r"); break;
                    case '\t': sb.Append("\\t"); break;
                    default:
                        if (c < ' ') sb.Append("\\u").Append(((int)c).ToString("x4"));
                        else sb.Append(c);
                        break;
                }
            }
            sb.Append('"');
        }

        // ---------- parsing ----------
        public static object Parse(string text)
        {
            int pos = 0;
            object result = ParseValue(text, ref pos);
            return result;
        }

        private static void SkipWs(string s, ref int p)
        {
            while (p < s.Length && char.IsWhiteSpace(s[p])) p++;
        }

        private static object ParseValue(string s, ref int p)
        {
            SkipWs(s, ref p);
            if (p >= s.Length) return null;
            char c = s[p];
            switch (c)
            {
                case '{': return ParseObject(s, ref p);
                case '[': return ParseArray(s, ref p);
                case '"': return ParseString(s, ref p);
                case 't': p += 4; return true;
                case 'f': p += 5; return false;
                case 'n': p += 4; return null;
                default: return ParseNumber(s, ref p);
            }
        }

        private static Dictionary<string, object> ParseObject(string s, ref int p)
        {
            var map = new Dictionary<string, object>();
            p++; // {
            SkipWs(s, ref p);
            if (p < s.Length && s[p] == '}') { p++; return map; }
            while (p < s.Length)
            {
                SkipWs(s, ref p);
                string key = ParseString(s, ref p);
                SkipWs(s, ref p);
                if (p < s.Length && s[p] == ':') p++;
                object val = ParseValue(s, ref p);
                map[key] = val;
                SkipWs(s, ref p);
                if (p < s.Length && s[p] == ',') { p++; continue; }
                if (p < s.Length && s[p] == '}') { p++; break; }
                break;
            }
            return map;
        }

        private static List<object> ParseArray(string s, ref int p)
        {
            var list = new List<object>();
            p++; // [
            SkipWs(s, ref p);
            if (p < s.Length && s[p] == ']') { p++; return list; }
            while (p < s.Length)
            {
                object val = ParseValue(s, ref p);
                list.Add(val);
                SkipWs(s, ref p);
                if (p < s.Length && s[p] == ',') { p++; continue; }
                if (p < s.Length && s[p] == ']') { p++; break; }
                break;
            }
            return list;
        }

        private static string ParseString(string s, ref int p)
        {
            var sb = new StringBuilder();
            p++; // opening quote
            while (p < s.Length)
            {
                char c = s[p++];
                if (c == '"') break;
                if (c == '\\' && p < s.Length)
                {
                    char e = s[p++];
                    switch (e)
                    {
                        case '"': sb.Append('"'); break;
                        case '\\': sb.Append('\\'); break;
                        case '/': sb.Append('/'); break;
                        case 'b': sb.Append('\b'); break;
                        case 'f': sb.Append('\f'); break;
                        case 'n': sb.Append('\n'); break;
                        case 'r': sb.Append('\r'); break;
                        case 't': sb.Append('\t'); break;
                        case 'u':
                            if (p + 4 <= s.Length)
                            {
                                int code = int.Parse(s.Substring(p, 4), NumberStyles.HexNumber, CultureInfo.InvariantCulture);
                                sb.Append((char)code);
                                p += 4;
                            }
                            break;
                        default: sb.Append(e); break;
                    }
                }
                else sb.Append(c);
            }
            return sb.ToString();
        }

        private static object ParseNumber(string s, ref int p)
        {
            int start = p;
            while (p < s.Length && (char.IsDigit(s[p]) || "+-.eE".IndexOf(s[p]) >= 0)) p++;
            string num = s.Substring(start, p - start);
            if (double.TryParse(num, NumberStyles.Any, CultureInfo.InvariantCulture, out double d)) return d;
            return 0.0;
        }

        // ---------- typed helpers ----------
        public static string GetString(IDictionary<string, object> map, string key, string def)
        {
            if (map != null && map.TryGetValue(key, out var v) && v is string s) return s;
            return def;
        }

        public static bool GetBool(IDictionary<string, object> map, string key, bool def)
        {
            if (map != null && map.TryGetValue(key, out var v) && v is bool b) return b;
            return def;
        }

        public static double GetDouble(IDictionary<string, object> map, string key, double def)
        {
            if (map != null && map.TryGetValue(key, out var v) && v is double d) return d;
            return def;
        }
    }
}
