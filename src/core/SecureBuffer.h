#pragma once

#include <QByteArray>
#include <sodium.h>
#include <utility>

// RAII wrapper around QByteArray that calls sodium_memzero on destruction.
// Use for any secret material: keys, plaintext passwords, derived bytes.
class SecureBuffer
{
public:
    SecureBuffer() = default;

    explicit SecureBuffer(int size)
        : m_data(size, '\0')
    {}

    // Take ownership of existing bytes (zeroes the source).
    explicit SecureBuffer(QByteArray &&src) noexcept
        : m_data(std::move(src))
    {}

    // Copy from QByteArray — caller should zero their copy if needed.
    explicit SecureBuffer(const QByteArray &src)
        : m_data(src)
    {}

    ~SecureBuffer() { wipe(); }

    // Move OK — source is left empty.
    SecureBuffer(SecureBuffer &&other) noexcept
        : m_data(std::move(other.m_data))
    {}

    SecureBuffer &operator=(SecureBuffer &&other) noexcept
    {
        if (this != &other) {
            wipe();
            m_data = std::move(other.m_data);
        }
        return *this;
    }

    // No copy — forces explicit intent.
    SecureBuffer(const SecureBuffer &) = delete;
    SecureBuffer &operator=(const SecureBuffer &) = delete;

    // Wipe and release.
    void wipe()
    {
        if (!m_data.isEmpty()) {
            sodium_memzero(m_data.data(), m_data.size());
            m_data.clear();
        }
    }

    // Access
    char       *data()       { return m_data.data(); }
    const char *constData() const { return m_data.constData(); }
    int         size()  const { return m_data.size(); }
    bool        isEmpty() const { return m_data.isEmpty(); }
    void        resize(int n) { m_data.resize(n, '\0'); }

    // Convenience: return a copy as QByteArray (caller owns the copy).
    QByteArray toByteArray() const { return m_data; }

    // Allow implicit read as const QByteArray& for passing to libsodium/Qt APIs.
    const QByteArray &ref() const { return m_data; }

private:
    QByteArray m_data;
};
