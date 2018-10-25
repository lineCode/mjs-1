#include "value.h"
#include <ostream>
#include <stdexcept>
#include <sstream>
#include <cmath>
#include <cstring>

namespace mjs {

const value value::undefined{value_type::undefined};
const value value::null{value_type::null};

//
// value_type
//

const char* string_value(value_type t) {
    switch (t) {
    case value_type::undefined: return "undefined";
    case value_type::null:      return "null";
    case value_type::boolean:   return "boolean";
    case value_type::number:    return "number";
    case value_type::string:    return "string";
    case value_type::object:    return "object";
    case value_type::reference: return "reference";
    case value_type::native_function: return "native_function";
    }
    NOT_IMPLEMENTED((int)t);
}

//
// reference
//

const value& reference::get_value() const {
    return base_->get(property_name_);
}

void reference::put_value(const value& val) const {
    base_->put(property_name_, val);
}

//
// value
//

value::value(const value& rhs) : type_(value_type::undefined) { *this = rhs; }
value::value(value&& rhs) : type_(value_type::undefined) { *this = std::move(rhs); }

value& value::operator=(const value& rhs) {
    if (this != &rhs) {
        destroy();
        switch (rhs.type_) {
        case value_type::undefined: break;
        case value_type::null:      break;
        case value_type::boolean:   b_ = rhs.b_; break;
        case value_type::number:    n_ = rhs.n_; break;
        case value_type::string:    new (&s_) string{rhs.s_}; break;
        case value_type::object:    new (&o_) object_ptr{rhs.o_}; break;
        case value_type::reference: new (&r_) reference{rhs.r_}; break;
        case value_type::native_function: new (&f_) native_function_type{rhs.f_}; break;
        default: NOT_IMPLEMENTED(rhs.type_);
        }
        type_ = rhs.type_;
    }
    return *this;
}
value& value::operator=(value&& rhs) {
    destroy();
    switch (rhs.type_) {
    case value_type::undefined: break;
    case value_type::null:      break;
    case value_type::boolean:   b_ = rhs.b_; break;
    case value_type::number:    n_ = rhs.n_; break;
    case value_type::string:    new (&s_) string{std::move(rhs.s_)}; break;
    case value_type::object:    new (&o_) object_ptr{std::move(rhs.o_)}; break;
    case value_type::reference: new (&r_) reference{std::move(rhs.r_)}; break;
    case value_type::native_function: new (&f_) native_function_type{std::move(rhs.f_)}; break;
    default: NOT_IMPLEMENTED(rhs.type_);
    }
    type_ = rhs.type_;
    rhs.type_ = value_type::undefined;
    return *this;
}

void value::destroy() {
    switch (type_) {
    case value_type::undefined: break;
    case value_type::null: break;
    case value_type::boolean: break;
    case value_type::number: break;
    case value_type::string: s_.~string(); break;
    case value_type::object: o_.~shared_ptr(); break;
    case value_type::reference: r_.~reference(); break;
    case value_type::native_function: f_.~function(); break;
    default: NOT_IMPLEMENTED(type_);
    }
    type_ = value_type::undefined;
}

std::ostream& operator<<(std::ostream& os, const value& v) {
    return os << to_string(v);
}

std::wostream& operator<<(std::wostream& os, const value& v) {
    return os << to_string(v);
}

bool operator==(const value& l, const value& r) {
    if (l.type() != r.type()) {
        return false;
    }

    switch (l.type()) {
    case value_type::undefined: return true;
    case value_type::null:      return true;
    case value_type::boolean:   return l.boolean_value() == r.boolean_value();
    case value_type::number: {
        const double lv = l.number_value(), rv = r.number_value();
        return lv == rv || (std::isnan(lv) && std::isnan(rv));
    }
    case value_type::string:    return l.string_value().view() == r.string_value().view();
    case value_type::object:    return l.object_value() == r.object_value();
    case value_type::reference: break;
    case value_type::native_function: break;
    }
    NOT_IMPLEMENTED(l.type());
}

[[nodiscard]] bool put_value(const value& ref, const value& val) {
    if (ref.type() != value_type::reference) return false;
    ref.reference_value().put_value(val);
    return true;
}

//
// Type Conversions
//

value to_primitive(const value& v, value_type hint) {
    if (v.type() != value_type::object) {
        return v;
    }
    return v.object_value()->default_value(hint);
}

bool to_boolean(const value& v) {
    switch (v.type()) {
    case value_type::undefined: return false;
    case value_type::null:      return false;
    case value_type::boolean:   return v.boolean_value();
    case value_type::number:    return v.number_value() != 0 && !std::isnan(v.number_value());
    case value_type::string:    return !v.string_value().view().empty();
    case value_type::object:    return true;
    case value_type::reference: break;
    case value_type::native_function: break;
    }
    NOT_IMPLEMENTED(v.type());
}

double to_number(const value& v) {
    switch (v.type()) {
    case value_type::undefined: return NAN;
    case value_type::null:      return +0.0;
    case value_type::boolean:   return v.boolean_value() ? 1.0 : +0.0;
    case value_type::number:    return v.number_value();
    case value_type::string:    return to_number(v.string_value());
    case value_type::object:    return to_number(to_primitive(v, value_type::number));
    case value_type::reference: break;
    case value_type::native_function: break;
    }
    NOT_IMPLEMENTED(v.type());
}

double to_integer_inner(double n) {
    return (n < 0 ? -1 : 1) * std::floor(std::abs(n));
}

double to_integer(double n) {
    if (std::isnan(n)) {
        return 0;
    }
    if (n == 0 || std::isinf(n)) {
        return n;
    }
    return to_integer_inner(n);
}

double to_integer(const value& v) {
    return to_integer(to_number(v));
}

int32_t to_int32(double n) {
    return static_cast<int32_t>(to_uint32(n));
}

int32_t to_int32(const value& v) {
    return to_int32(to_number(v));
}

uint32_t to_uint32(double n) {
    if (std::isnan(n) || n == 0 || std::isinf(n)) {
        return 0;
    }
    n = to_integer_inner(n);
    constexpr double max = 1ULL << 32;
    n = n - max * std::floor(n / max);
    assert(n >= 0.0 && n < max);
    return static_cast<uint32_t>(n);
}

uint32_t to_uint32(const value& v) {
    return to_uint32(to_number(v));
}

uint16_t to_uint16(double n) {
    return static_cast<uint16_t>(to_uint32(n));
}

uint16_t to_uint16(const value& v) {
    return to_uint16(to_number(v));
}

double blah(double n, int incr) {
    uint64_t i;
    memcpy(&i, &n, sizeof(double));
    i += incr;
    memcpy(&n, &i, sizeof(double));
    return n;
}

#if 0
printf("Converting %.17g\n", m);
printf("%s\n", foo(blah(m, -1)).c_str());
printf("%s\n", foo(blah(m, 0)).c_str());
printf("%s\n", foo(blah(m, 1)).c_str());

std::string foo(double m) {
    std::ostringstream oss;
    oss.precision(17);
    char buffer[_CVTBUFSIZE + 1];
    int decimal_point, sign;
    _ecvt_s(buffer, m, 22, &decimal_point, &sign);
    assert(sign == 0);
    oss.width(40);
    oss  << std::defaultfloat << m << " -> '" << buffer << "' decimal_point " << decimal_point;
    return oss.str();
}
#endif

string do_format_double(double m, int k) {
    assert(k >= 1);             // k is the number of decimal digits in the representation
    int n;                      // n is the position of the decimal point in s
    int sign;                   // sign is set if the value is negative (never true since to_string handles that)
#ifdef _MSC_VER
    char s[_CVTBUFSIZE + 1];    // s is the decimal representation of the number
    _ecvt_s(s, m, k, &n, &sign);
#else
    const char* s = ecvt(m, k, &n, &sign);
#endif
    assert(sign == 0);

    std::wostringstream woss;
    if (k <= n && n <= 21) {
        // 6. If k <= n <= 21, return the string consisting of the k digits of the decimal
        // representation of s (in order, with no leading zeroes), followed by n - k
        // occurences of the character �0�
        woss << s << std::wstring(n-k, '0');
    } else if (0 < n && n <= 21) {
        // 7. If 0 < n <= 21, return the string consisting of the most significant n digits
        // of the decimal representation of s, followed by a decimal point �.�, followed
        // by the remaining k - n digits of the decimal representation of s.
        woss << std::wstring(s, s + n) << '.' << std::wstring(s + n, s + std::strlen(s));
    } else if (-6 < n && n <= 0) {
        // 8. If -6 < n <= 0, return the string consisting of the character �0�, followed
        // by a decimal point �.�, followed by -n occurences of the character �0�, followed
        // by the k digits of the decimal representation of s.
        woss << "0." << std::wstring(-n, '0') << s;
    } else if (k == 1) {
        // 9.  Otherwise, if k = 1, return the string consisting of the single digit of s,
        // followed by lowercase character �e�, followed by a plus sign �+� or minus sign
        // �-� according to whether n - 1 is positive or negative, followed by the decimal
        // representation of the integer abs(n - 1) (with no leading zeros).
        woss << s << 'e' << (n-1>=0?'+':'-') << std::abs(n-1);
    } else {
        // 10. Return the string consisting of the most significant digit of the decimal
        // representation of s, followed by a decimal point �.�, followed by the remaining
        // k - 1 digits of the decimal representation of s , followed by the lowercase character
        // �e�, followed by a plus sign �+� or minus sign �-� according to whether n - 1 is positive
        // or negative, followed by the decimal representation of the integer abs(n - 1)
        // (with no leading zeros)
        woss << s[0] << '.' << s+1 << 'e' << (n-1>=0?'+':'-') << std::abs(n-1);
    }
    return string{woss.str()};
}

string to_string(double m) {
    // Handle special cases
    if (std::isnan(m)) {
        return string{"NaN"};
    }
    if (m == 0) {
        return string{"0"};
    }
    if (m < 0) {
        return string{"-"} + to_string(-m);
    }
    if (std::isinf(m)) {
        return string{"Infinity"};
    }

    assert(std::isfinite(m) && m > 0);

    // 9.8.1 ToString Applied to the Number Type    

    // Use really slow method to determine shortest representation of m
    for (int k = 1; k <= 17; ++k) {
        std::ostringstream woss;
        woss.precision(k);
        woss << std::defaultfloat << m;
        if (double t; std::istringstream{woss.str()} >> t && t == m) {
            return do_format_double(m, k);
        }
    }
    // More than 17 digits should never happen
    assert(false);
    NOT_IMPLEMENTED(m);
}

string to_string(const value& v) {
    switch (v.type()) {
    case value_type::undefined: return string{"undefined"};
    case value_type::null:      return string{"null"};
    case value_type::boolean:   return string{v.boolean_value() ? "true" : "false"};
    case value_type::number:    return to_string(v.number_value());
    case value_type::string:    return v.string_value();
    case value_type::object:    return to_string(to_primitive(v, value_type::string));
    case value_type::reference: break;
    case value_type::native_function: break;
    }
    NOT_IMPLEMENTED(v.type());
}

std::unordered_set<object*> object::all_objects_;

void object::gc_visit(std::unordered_set<const object*>& live_objects) const {
    live_objects.insert(this);
    for (const auto& p: properties_) {
        if (p.second.val.type() == value_type::object) {
            p.second.val.object_value()->gc_visit(live_objects);
        }
    }
}

void object::garbage_collect(const std::vector<object_ptr>& roots) {
    std::unordered_set<const object*> live_objects;
    for (const auto& o : roots) {
        o->gc_visit(live_objects);
    }
    std::vector<std::weak_ptr<object>> to_delete;

    for (auto it = all_objects_.begin(); it != all_objects_.end(); ++it) {
        if (live_objects.find(*it) == live_objects.end()) {
            to_delete.push_back((*it)->shared_from_this());
        }
    }
    for (const auto& o: to_delete) {
        if (auto p = o.lock()) {
            p->clear();
        }
    }
}

void object::clear() {
    prototype_.reset();
    value_ = value::undefined;
    construct_ = native_function_type{};
    call_ = native_function_type{};
    properties_.clear();
}

void debug_print(std::wostream& os, const value& v, int indent_incr, int max_nest, int indent) {
    switch (v.type()) {
    case value_type::object:
        if (!v.object_value()) {
            os << "[Object null]";
            return;
        }
        v.object_value()->debug_print(os, indent_incr, max_nest, indent);
        break;
    default:
        os << "[" << v.type() << " " <<  v << "]";
    }
}

void object::debug_print(std::wostream& os, int indent_incr, int max_nest, int indent) const {
    if (indent / indent_incr > 4 || max_nest <= 0) {
        os << "[Object " << class_ << "]";
        return;
    }
    auto indent_string = std::wstring(indent + indent_incr, ' ');
    auto print_prop = [&](const auto& name, const auto& val, bool internal) {
        os << indent_string << name << ": ";
        if constexpr (std::is_same_v<const string&, decltype(val)>) {
            (void)internal;
            os << val;
        } else {
            mjs::debug_print(os, mjs::value{val}, indent_incr, internal ? 1 : max_nest - 1, indent + indent_incr);
        }
        os << "\n";
    };
    os << "{\n";
    for (const auto& p : properties_) {
        if (p.first.view() == L"constructor") {
            print_prop(p.first, p.second.val, true);
        } else {
            print_prop(p.first, p.second.val, false);
        }
    }
    print_prop("[[Class]]", class_, true);
    print_prop("[[Prototype]]", prototype_, true);
    if (value_.type() != value_type::undefined) {
        print_prop("[[Value]]", value_, true);
    }
    os << std::wstring(indent, ' ') << "}\n";
}

[[noreturn]] void throw_runtime_error(const std::string_view& s, const char* file, int line) {
    std::ostringstream oss;
    oss << file << ":" << line << ": " << s;
    throw std::runtime_error(oss.str());
}

[[noreturn]] void throw_runtime_error(const std::wstring_view& s, const char* file, int line) {
    throw_runtime_error(std::string(s.begin(), s.end()), file, line);
}


} // namespace mjs