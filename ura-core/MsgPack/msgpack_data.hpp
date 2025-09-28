#pragma once

#include <algorithm>
#include <sstream>
#include <chrono>
#include <iostream>
#include <random>
#include <unordered_map>
#include <queue>
#include <msgpack11.hpp>


#include "il2cpp/il2cpp_symbols.hpp"

using namespace std;
using namespace msgpack11;
//using namespace Microsoft::WRL;

namespace MsgPackData
{

	MsgPack::object user_info;	
	MsgPack::object tp_info;
	MsgPack::object rp_info; //

	MsgPack::array jobs_going_info_array;

	int BinToInt(const std::string& bin) {
		int value = 0;
		for (size_t i = 0; i < bin.size(); i++) {
			value |= (static_cast<unsigned char>(bin[i]) << (8 * i)); 
		}
		return value;
	}

	std::string BinToHex(const std::vector<uint8_t>& bin) {
		static const char* hex = "0123456789abcdef";
		std::string out;
		for (uint8_t c : bin) {
			out.push_back(hex[c >> 4]);
			out.push_back(hex[c & 0xF]);
		}
		return out;
	}

	uint64_t ByteSwap64(uint64_t value) {
		uint8_t bytes[8];
		memcpy(bytes, &value, sizeof(uint64_t));
		std::reverse(bytes, bytes + sizeof(uint64_t));

		uint64_t result;
		memcpy(&result, bytes, sizeof(uint64_t));
		return result;
	}

	// Helper to safely read a 64-bit integer from a byte vector
	uint64_t ReadUInt64FromBytes(const std::vector<uint8_t>& bytes) {
		uint64_t value = 0;
		if (bytes.size() >= sizeof(uint64_t)) {
			memcpy(&value, bytes.data(), sizeof(uint64_t));
		}
		return value;
	}


	std::string ToJsonSafe(const msgpack11::MsgPack& obj, int indent_level = 0);

	std::string indent(int level) {
		return std::string(level * 2, ' ');
	}

	std::string ToJsonSafe(const msgpack11::MsgPack& obj, int indent_level) {
		std::stringstream ss;
		ss.imbue(std::locale::classic());

		switch (obj.type()) {
		case msgpack11::MsgPack::Type::NUL:
			ss << "null";
			break;
		case msgpack11::MsgPack::Type::BOOL:
			ss << (obj.bool_value() ? "true" : "false");
			break;
		case msgpack11::MsgPack::Type::INT:
		case msgpack11::MsgPack::Type::INT8:
		case msgpack11::MsgPack::Type::INT16:
		case msgpack11::MsgPack::Type::INT32:
		case msgpack11::MsgPack::Type::INT64:
		case msgpack11::MsgPack::Type::UINT8:
		case msgpack11::MsgPack::Type::UINT16:
		case msgpack11::MsgPack::Type::UINT32:
		case msgpack11::MsgPack::Type::UINT64:
		case msgpack11::MsgPack::Type::FLOAT32:
		case msgpack11::MsgPack::Type::FLOAT64:
			ss << static_cast<long long>(obj.number_value());
			break;
		case msgpack11::MsgPack::Type::STRING: {
			std::string s = obj.string_value();
			ss << "\"";
			for (char c : s) {
				if (c == '\"' || c == '\\') {
					ss << '\\';
				}
				ss << c;
			}
			ss << "\"";

			break;
		}
		case msgpack11::MsgPack::Type::BINARY: {
			const auto& bin = obj.binary_items();
			ss << "\"0x" << BinToHex(bin) << "\"";
			break;
		}
		case msgpack11::MsgPack::Type::ARRAY: {
			const auto& items = obj.array_items();
			if (items.empty()) {
				ss << "[]";
				break;
			}
			ss << "[\n";
			for (size_t i = 0; i < items.size(); ++i) {
				ss << indent(indent_level + 1) << ToJsonSafe(items[i], indent_level + 1);
				if (i < items.size() - 1) {
					ss << ",";
				}
				ss << "\n";
			}
			ss << indent(indent_level) << "]";
			break;
		}
		case msgpack11::MsgPack::Type::OBJECT: {
			const auto& items = obj.object_items();
			if (items.empty()) {
				ss << "{}";
				break;
			}
			ss << "{\n";
			bool first = true;
			for (const auto& kv : items) {
				if (!first) {
					ss << ",\n";
				}
				ss << indent(indent_level + 1);
				if (kv.first.is_string()) {
					ss << "\"" << kv.first.string_value() << "\": ";
				}
				else {
					ss << ToJsonSafe(kv.first, indent_level + 1) << ": ";
				}
				ss << ToJsonSafe(kv.second, indent_level + 1);
				first = false;
			}
			ss << "\n" << indent(indent_level) << "}";
			break;
		}
		// Handle extension types, which are common in games for custom data
		case msgpack11::MsgPack::Type::EXTENSION: {
			// Not sure this is ever used?
			const auto& ext = obj.extension_items();
			printf("We have an object with an extension type\n");
			//ss << "{\"extension_type\": " << static_cast<int>(ext.type())
			//	<< ", \"data\": \"0x" << BinToHex(ext.data()) << "\"}";
			break;
		}
		default:
			int unknown_type_id = static_cast<int>(obj.type());
			if (unknown_type_id == 47) {
				std::string raw_data = obj.dump();
				
				uint64_t raw_viewer_id = 0;
				memcpy(&raw_viewer_id, raw_data.data() + 1, sizeof(uint64_t));

				// big-endian, so we must swap the bytes (thanks Gemini).
				uint64_t correct_viewer_id = ByteSwap64(raw_viewer_id);

				return "\"" + std::to_string(correct_viewer_id) + "\"";
			}
			else {
				ss << "\"" << static_cast<int>(obj.type()) << ">\"";
			}
			break;
		}
		return ss.str();
	}

	void DisplayMsgPackData(const char* buffer, size_t length) {
		if (!buffer) {
			std::cout << "[MsgPack Viewer] Error: Buffer is null." << std::endl;
			return;
		}

		try {
			std::istringstream data_stream(std::string(buffer, length));

			std::string err;
			const msgpack11::MsgPack parsed_data = msgpack11::MsgPack::parse(data_stream, err);

			if (!err.empty()) {
				std::cout << "[MsgPack Viewer] Parse Error: " << err << std::endl;
				return;
			}

			std::cout << "--- Begin MsgPack Data ---\n"
				<< ToJsonSafe(parsed_data)
				<< "\n--- End MsgPack Data ---" << std::endl;

		}
		catch (const std::exception& e) {
			std::cout << "[MsgPack Viewer] A critical exception occurred during parsing: "
				<< e.what() << std::endl;
		}
	}

	void SaveMsgPackData(const char* buffer, size_t length, const std::string& prefix = "response") {
		if (!buffer) {
			printf("[MsgPack Saver] Error: Buffer is null.\n");
			return;
		}

		if (length == 0) {
			printf("[MsgPack Saver] Error: Buffer length is zero.\n");
			return;
		}

		printf("[MsgPack Saver] Processing %s buffer of %zu bytes\n", prefix.c_str(), length);

		try {
			std::string err;
			std::string bin_data(buffer, length);  // binary safe


			msgpack11::MsgPack parsed_data = msgpack11::MsgPack::parse(bin_data, err);

			if (!err.empty()) {
				printf("[MsgPack Saver] Parse Error for %s (%zu bytes): %s\n", prefix.c_str(), length, err.c_str());
				return;
			}

			if (parsed_data.is_object()) {
				for (auto& kv : parsed_data.object_items()) {
					//printf("[Key] %s\n", kv.first.string_value());
				}
			}

			auto now = std::chrono::system_clock::now();
			auto UTC = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();

			std::string filename = "Packets\\" + prefix + "_" + std::to_string(UTC) + ".json";

			std::ofstream out_file(filename);
			if (out_file.is_open()) {
				//out_file << parsed_data.dump();
				out_file << ToJsonSafe(parsed_data);
				out_file.close();
				printf("[MsgPack Saver] Successfully saved data to %s\n", filename);
			}
			else {
				printf("[MsgPack Saver] Error: Could not open file %s for writing.\n", filename);
			}

		}
		catch (const std::exception& e) {
			printf("[MsgPack Saver] A critical exception occurred: %s\n", e.what());
		}
	}

	void SaveMsgPackData(const std::string& buffer_data, const std::string& prefix = "response") {
		if (buffer_data.empty()) {
			printf("[MsgPack Saver] Error: Buffer data is empty.\n");
			return;
		}

		printf("[MsgPack Saver] Processing %s buffer of %zu bytes\n", prefix.c_str(), buffer_data.size());

		try {
			std::string err;
			const msgpack11::MsgPack parsed_data = msgpack11::MsgPack::parse(buffer_data, err);

			if (!err.empty()) {
				printf("[MsgPack Saver] Parse Error for %s (%zu bytes): %s\n", prefix.c_str(), buffer_data.size(), err.c_str());
				return;
			}

			auto now = std::chrono::system_clock::now();
			auto UTC = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();

			string filename = "Packets\\" + prefix + "_" + std::to_string(UTC) + ".json";

			std::ofstream out_file(filename);
			if (out_file.is_open()) {
				//out_file << parsed_data.dump();
				out_file << ToJsonSafe(parsed_data);
				out_file.close();
				printf("[MsgPack Saver] Successfully saved data to %s\n", filename);
			}
			else {
				printf("[MsgPack Saver] Error: Could not open file %s for writing.\n", filename);
			}
		}
		catch (const std::exception& e) {
			printf("[MsgPack Saver] A critical exception occurred: %s\n", e.what());
		}
	}

}