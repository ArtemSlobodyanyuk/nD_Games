#pragma once

// Невеликий суворий читач JSON для правил, який не потребує сторонніх бібліотек.
// Підтримує рядки ASCII, булеві значення, цілі числа, масиви та об'єкти.
// Значення null, дробові числа й послідовності Unicode у цій схемі не використовуються.
#include <cctype>
#include <climits>
#include <map>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace nd::json {
// Одне значення JSON. std::variant зберігає рівно один із перелічених типів.
// Масив містить значення за порядком, об'єкт зіставляє рядкові ключі зі значеннями.
struct Value {
    using Array = std::vector<Value>;
    using Object = std::map<std::string, Value>;
    std::variant<bool, int, std::string, Array, Object> data;
    // Безпечно читає значення як тип T; невідповідність типів спричиняє виняток.
    template<class T> const T& as() const {
        const auto* result = std::get_if<T>(&data);
        if (!result) throw std::runtime_error("JSON value has wrong type");
        return *result;
    }
    // Читає обов'язкове поле об'єкта та повідомляє, якого саме ключа не вистачає.
    const Value& at(const std::string& key) const {
        const auto& object = as<Object>();
        auto it = object.find(key);
        if (it == object.end()) throw std::runtime_error("Missing rules field: " + key);
        return it->second;
    }
};
// Послідовно читає текст зліва направо. Вкладені масиви й об'єкти читає рекурсивно.
class Parser {
public:
    // Зберігає посилання на текст: він має існувати до завершення parse().
    explicit Parser(const std::string& input) : input_(input) {}
    // У документі дозволене одне кореневе значення та пробіли навколо нього.
    Value parse() {
        Value result = value(0);
        space();
        // Після кореневого значення не повинно залишитися стороннього тексту.
        if (offset_ != input_.size()) fail();
        return result;
    }
private:
    const std::string& input_;
    // Індекс наступного ще не прочитаного байта тексту.
    std::size_t offset_ = 0;
    // Єдина точка синтаксичної помилки. [[noreturn]] означає, що функція не повертається.
    [[noreturn]] void fail() const {
        throw std::runtime_error("Invalid rules JSON at byte " + std::to_string(offset_));
    }
    // JSON дозволяє як пропуски лише пробіл, перенесення рядка й табуляцію.
    void space() {
        while (offset_ < input_.size() && (input_[offset_] == ' ' || input_[offset_] == '\n'
               || input_[offset_] == '\r' || input_[offset_] == '\t')) ++offset_;
    }
    // Пропускає пробіли й споживає очікуваний символ, якщо він присутній.
    bool take(char c) {
        space();
        if (offset_ < input_.size() && input_[offset_] == c) { ++offset_; return true; }
        return false;
    }
    // Читає текст між лапками та розпізнає підтримувані зворотні послідовності.
    std::string string() {
        if (!take('"')) fail();
        std::string result;
        while (offset_ < input_.size()) {
            // Беззнаковий байт дозволяє коректно перевіряти діапазон символів.
            unsigned char c = static_cast<unsigned char>(input_[offset_++]);
            if (c == '"') return result;
            if (c < 32 || c > 126) fail();
            if (c == '\\') {
                if (offset_ == input_.size()) fail();
                char escape = input_[offset_++];
                // Наприклад, два символи '\\' та 'n' перетворюються на перенесення рядка.
                switch (escape) {
                case '"': case '\\': case '/': result += escape; break;
                case 'n': result += '\n'; break;
                case 'r': result += '\r'; break;
                case 't': result += '\t'; break;
                case 'b': result += '\b'; break;
                case 'f': result += '\f'; break;
                default: fail();
                }
            } else result += static_cast<char>(c);
        }
        fail();
    }
    // Обирає спосіб читання за першим символом поточного значення.
    Value value(int depth) {
        // Обмеження вкладеності захищає стек від надто глибокого документа.
        if (depth > 24) fail();
        space();
        if (offset_ == input_.size()) fail();
        char c = input_[offset_];
        if (c == '"') return Value{string()};
        // Об'єкт: послідовність пар "ключ": значення, розділених комами.
        if (take('{')) {
            Value::Object result;
            if (take('}')) return Value{result};
            do {
                std::string key = string();
                if (!take(':')) fail();
                // emplace повертає false у другій частині результату для повтореного ключа.
                if (!result.emplace(key, value(depth + 1)).second) fail();
                if (take('}')) return Value{result};
            } while (take(','));
            fail();
        }
        // Масив: послідовність значень довільного підтримуваного типу.
        if (take('[')) {
            Value::Array result;
            if (take(']')) return Value{result};
            do {
                result.push_back(value(depth + 1));
                if (take(']')) return Value{result};
            } while (take(','));
            fail();
        }
        // Булеві літерали в JSON мають лише такі малі літери.
        if (input_.compare(offset_, 4, "true") == 0) { offset_ += 4; return Value{true}; }
        if (input_.compare(offset_, 5, "false") == 0) { offset_ += 5; return Value{false}; }
        bool negative = take('-');
        // Між мінусом і цифрою не може бути пробілів; початкові зайві нулі заборонені.
        if (offset_ == input_.size() || !std::isdigit(static_cast<unsigned char>(input_[offset_]))) fail();
        bool zero = input_[offset_] == '0';
        // Ширший проміжний тип дозволяє виявити вихід за межі int до перетворення.
        long long number = 0;
        do {
            number = number * 10 + input_[offset_++] - '0';
            if (number > INT_MAX) fail();
            if (zero) break;
        } while (offset_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[offset_])));
        return Value{static_cast<int>(negative ? -number : number)};
    }
};
} // Кінець простору імен читача JSON.
