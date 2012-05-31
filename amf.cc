#include "amf.h"
#include "utils.h"
#include <stdexcept>
#include <string.h>
#include <arpa/inet.h>

namespace {

uint8_t get_byte(Decoder *dec)
{
	if (dec->pos >= dec->buf.size()) {
		throw std::runtime_error("Not enough data");
	}
	return uint8_t(dec->buf[dec->pos++]);
}

uint8_t peek(const Decoder *dec)
{
	if (dec->pos >= dec->buf.size()) {
		throw std::runtime_error("Not enough data");
	}
	return uint8_t(dec->buf[dec->pos]);
}

}

AMFValue::AMFValue(AMFType type) :
	m_type(type)
{
	switch (m_type) {
	case AMF_OBJECT:
	case AMF_ECMA_ARRAY:
		m_value.object = new amf_object_t;
		break;
	case AMF_NUMBER:
	case AMF_NULL:
		break;
	default:
		assert(0);
	}
}

AMFValue::AMFValue(const std::string &s) :
	m_type(AMF_STRING)
{
	m_value.string = new std::string(s);
}

AMFValue::AMFValue(double n) :
	m_type(AMF_NUMBER)
{
	m_value.number = n;
}

AMFValue::AMFValue(bool b) :
	m_type(AMF_BOOLEAN)
{
	m_value.boolean = b;
}

AMFValue::AMFValue(const amf_object_t &object) :
	m_type(AMF_OBJECT)
{
	m_value.object = new amf_object_t(object);
}

AMFValue::AMFValue(const AMFValue &from) :
	m_type(AMF_NULL)
{
	*this = from;
}

AMFValue::~AMFValue()
{
	destroy();
}

void AMFValue::destroy()
{
	switch (m_type) {
	case AMF_STRING:
		delete m_value.string;
		break;
	case AMF_OBJECT:
	case AMF_ECMA_ARRAY:
		delete m_value.object;
		break;
	case AMF_NULL:
	case AMF_NUMBER:
	case AMF_BOOLEAN:
		break;
	}
}

void AMFValue::operator = (const AMFValue &from)
{
	destroy();
	m_type = from.m_type;
	switch (m_type) {
	case AMF_STRING:
		m_value.string = new std::string(*from.m_value.string);
		break;
	case AMF_OBJECT:
	case AMF_ECMA_ARRAY:
		m_value.object = new amf_object_t(*from.m_value.object);
		break;
	case AMF_NUMBER:
		m_value.number = from.m_value.number;
		break;
	case AMF_BOOLEAN:
		m_value.boolean = from.m_value.boolean;
		break;
	default:
		break;
	}
}

void amf_write(Encoder *enc, const std::string &s)
{
	enc->buf += char(AMF0_STRING);
	uint16_t str_len = htons(s.size());
	enc->buf.append((char *) &str_len, 2);
	enc->buf += s;
}

void amf_write(Encoder *enc, double n)
{
	enc->buf += char(AMF0_NUMBER);
	uint64_t encoded = 0;
#if defined(__i386__) || defined(__x86_64__)
	/* Flash uses same floating point format as x86 */
	memcpy(&encoded, &n, 8);
#endif
	uint32_t val = htonl(encoded >> 32);
	enc->buf.append((char *) &val, 4);
	val = htonl(encoded);
	enc->buf.append((char *) &val, 4);
}

void amf_write(Encoder *enc, bool b)
{
	enc->buf += char(AMF0_BOOLEAN);
	enc->buf += char(b);
}

void amf_write_key(Encoder *enc, const std::string &s)
{
	uint16_t str_len = htons(s.size());
	enc->buf.append((char *) &str_len, 2);
	enc->buf += s;
}

void amf_write(Encoder *enc, const amf_object_t &object)
{
	enc->buf += char(AMF0_OBJECT);
	for (amf_object_t::const_iterator i = object.begin();
					  i != object.end(); ++i) {
		amf_write_key(enc, i->first);
		amf_write(enc, i->second);
	}
	amf_write_key(enc, "");
	enc->buf += char(AMF0_OBJECT_END);
}

void amf_write_ecma(Encoder *enc, const amf_object_t &object)
{
	enc->buf += char(AMF0_ECMA_ARRAY);
	uint32_t zero = 0;
	enc->buf.append((char *) &zero, 4);
	for (amf_object_t::const_iterator i = object.begin();
					  i != object.end(); ++i) {
		amf_write_key(enc, i->first);
		amf_write(enc, i->second);
	}
	amf_write_key(enc, "");
	enc->buf += char(AMF0_OBJECT_END);
}

void amf_write_null(Encoder *enc)
{
	enc->buf += char(AMF0_NULL);
}

void amf_write(Encoder *enc, const AMFValue &value)
{
	switch (value.type()) {
	case AMF_STRING:
		amf_write(enc, value.as_string());
		break;
	case AMF_NUMBER:
		amf_write(enc, value.as_number());
		break;
	case AMF_BOOLEAN:
		amf_write(enc, value.as_boolean());
		break;
	case AMF_OBJECT:
		amf_write(enc, value.as_object());
		break;
	case AMF_ECMA_ARRAY:
		amf_write_ecma(enc, value.as_object());
		break;
	case AMF_NULL:
		enc->buf += char(AMF0_NULL);
		break;
	}
}

std::string amf_load_string(Decoder *dec)
{
	if (get_byte(dec) != AMF0_STRING) {
		throw std::runtime_error("Expected a string");
	}
	if (dec->pos + 2 > dec->buf.size()) {
		throw std::runtime_error("Not enough data");
	}
	size_t str_len = load_be16(&dec->buf[dec->pos]);
	dec->pos += 2;
	if (dec->pos + str_len > dec->buf.size()) {
		throw std::runtime_error("Not enough data");
	}
	std::string s(dec->buf, dec->pos, str_len);
	dec->pos += str_len;
	return s;
}

double amf_load_number(Decoder *dec)
{
	if (get_byte(dec) != AMF0_NUMBER) {
		throw std::runtime_error("Expected a string");
	}
	if (dec->pos + 8 > dec->buf.size()) {
		throw std::runtime_error("Not enough data");
	}
	uint64_t val = ((uint64_t) load_be32(&dec->buf[dec->pos]) << 32) |
			load_be32(&dec->buf[dec->pos + 4]);
	double n = 0;
#if defined(__i386__) || defined(__x86_64__)
	/* Flash uses same floating point format as x86 */
	memcpy(&n, &val, 8);
#endif
	dec->pos += 8;
	return n;
}

bool amf_load_boolean(Decoder *dec)
{
	if (get_byte(dec) != AMF0_BOOLEAN) {
		throw std::runtime_error("Expected a boolean");
	}
	return get_byte(dec) != 0;
}

std::string amf_load_key(Decoder *dec)
{
	if (dec->pos + 2 > dec->buf.size()) {
		throw std::runtime_error("Not enough data");
	}
	size_t str_len = load_be16(&dec->buf[dec->pos]);
	dec->pos += 2;
	if (dec->pos + str_len > dec->buf.size()) {
		throw std::runtime_error("Not enough data");
	}
	std::string s(dec->buf, dec->pos, str_len);
	dec->pos += str_len;
	return s;
}

amf_object_t amf_load_object(Decoder *dec)
{
	amf_object_t object;
	if (get_byte(dec) != AMF0_OBJECT) {
		throw std::runtime_error("Expected an object");
	}
	while (1) {
		std::string key = amf_load_key(dec);
		if (key.empty())
			break;
		AMFValue value = amf_load(dec);
		object.insert(std::make_pair(key, value));
	}
	if (get_byte(dec) != AMF0_OBJECT_END) {
		throw std::runtime_error("expected object end");
	}
	return object;
}

amf_object_t amf_load_ecma(Decoder *dec)
{
	/* ECMA array is the same as object, with 4 extra zero bytes */
	amf_object_t object;
	if (get_byte(dec) != AMF0_ECMA_ARRAY) {
		throw std::runtime_error("Expected an ECMA array");
	}
	if (dec->pos + 4 > dec->buf.size()) {
		throw std::runtime_error("Not enough data");
	}
	dec->pos += 4;
	while (1) {
		std::string key = amf_load_key(dec);
		if (key.empty())
			break;
		AMFValue value = amf_load(dec);
		object.insert(std::make_pair(key, value));
	}
	if (get_byte(dec) != AMF0_OBJECT_END) {
		throw std::runtime_error("expected object end");
	}
	return object;
}

AMFValue amf_load(Decoder *dec)
{
	uint8_t type = peek(dec);
	switch (type) {
	case AMF0_STRING:
		return AMFValue(amf_load_string(dec));
	case AMF0_NUMBER:
		return AMFValue(amf_load_number(dec));
	case AMF0_BOOLEAN:
		return AMFValue(amf_load_boolean(dec));
	case AMF0_OBJECT:
		return AMFValue(amf_load_object(dec));
	case AMF0_ECMA_ARRAY:
		return AMFValue(amf_load_ecma(dec));
	case AMF0_NULL:
		dec->pos++;
		return AMFValue(AMF_NULL);
	default:
		throw std::runtime_error("Unsupported AMF type");
	}
}
