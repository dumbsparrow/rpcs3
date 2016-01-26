#include "stdafx.h"
#include "PSF.h"

namespace psf
{
	_log::channel log("PSF");

	struct header_t
	{
		le_t<u32> magic;
		le_t<u32> version;
		le_t<u32> off_key_table;
		le_t<u32> off_data_table;
		le_t<u32> entries_num;
	};

	struct def_table_t
	{
		le_t<u16> key_off;
		le_t<format> param_fmt;
		le_t<u32> param_len;
		le_t<u32> param_max;
		le_t<u32> data_off;
	};

	const std::string& entry::as_string() const
	{
		CHECK_ASSERTION(type == format::string || type == format::array);
		return m_value_string;
	}

	u32 entry::as_integer() const
	{
		CHECK_ASSERTION(type == format::integer);
		return m_value_integer;
	}

	entry& entry::operator =(const std::string& value)
	{
		CHECK_ASSERTION(type == format::string || type == format::array);
		m_value_string = value;

		//if (m_max_size && m_value_string.size() > m_max_size)
		//{
		//	if (m_format != entry_format::string_not_null_term)
		//	{
		//		m_value_string.erase(m_max_size);
		//	}
		//	else
		//	{
		//		m_value_string.erase(m_max_size - 1);
		//	}
		//}

		return *this;
	}

	entry& entry::operator =(u32 value)
	{
		CHECK_ASSERTION(type == format::integer);
		m_value_integer = value;
		return *this;
	}

	u32 entry::size() const
	{
		switch (type)
		{
		case format::string:
		case format::array:
			return std::min(max_size, gsl::narrow<u32>(m_value_string.size() + (type == format::string)));

		case format::integer:
			return SIZE_32(u32);
		}

		throw EXCEPTION("entry::size(): invalid format (0x%x)", type);
	}

	registry load(const std::vector<char>& data)
	{
		// Check size
		CHECK_ASSERTION(data.size() >= sizeof(header_t));
		CHECK_ASSERTION((std::uintptr_t)data.data() % 8 == 0);

		// Get header
		const header_t& header = reinterpret_cast<const header_t&>(data[0]);

		// Check magic and version
		CHECK_ASSERTION(header.magic == *(u32*)"\0PSF");
		CHECK_ASSERTION(header.version == 0x101);
		CHECK_ASSERTION(sizeof(header_t) + header.entries_num * sizeof(def_table_t) <= header.off_key_table);
		CHECK_ASSERTION(header.off_key_table <= header.off_data_table);
		CHECK_ASSERTION(header.off_data_table <= data.size());

		// Get indices (alignment should be fine)
		const def_table_t* indices = reinterpret_cast<const def_table_t*>(data.data() + sizeof(header_t));

		// Load entries
		registry result;

		for (u32 i = 0; i < header.entries_num; ++i)
		{
			CHECK_ASSERTION(indices[i].key_off < header.off_data_table - header.off_key_table);

			// Get key name range
			const auto name_ptr = data.begin() + header.off_key_table + indices[i].key_off;
			const auto name_end = std::find(name_ptr , data.begin() + header.off_data_table, '\0');

			// Get name (must be unique)
			std::string key(name_ptr, name_end);

			CHECK_ASSERTION(result.count(key) == 0);
			CHECK_ASSERTION(indices[i].param_len <= indices[i].param_max);
			CHECK_ASSERTION(indices[i].data_off < data.size() - header.off_data_table);
			CHECK_ASSERTION(indices[i].param_max < data.size() - indices[i].data_off);

			// Get data pointer
			const auto value_ptr = data.begin() + header.off_data_table + indices[i].data_off;

			if (indices[i].param_fmt == format::integer && indices[i].param_max == sizeof(u32) && indices[i].param_len == sizeof(u32))
			{
				// Integer data
				result.emplace(std::piecewise_construct,
					std::forward_as_tuple(std::move(key)),
					std::forward_as_tuple(reinterpret_cast<const le_t<u32>&>(*value_ptr)));
			}
			else if (indices[i].param_fmt == format::string || indices[i].param_fmt == format::array)
			{
				// String/array data
				std::string value;

				if (indices[i].param_fmt == format::string)
				{
					// Find null terminator
					value.assign(value_ptr, std::find(value_ptr, value_ptr + indices[i].param_len, '\0'));
				}
				else
				{
					value.assign(value_ptr, value_ptr + indices[i].param_len);
				}

				result.emplace(std::piecewise_construct,
					std::forward_as_tuple(std::move(key)),
					std::forward_as_tuple(indices[i].param_fmt, indices[i].param_len, std::move(value)));
			}
			else
			{
				// Possibly unsupported format, entry ignored
				log.error("psf::load(): invalid format (key='%s', fmt=0x%x, len=0x%x, max=0x%x)", key, indices[i].param_fmt, indices[i].param_len, indices[i].param_max);
			}
		}

		return result;
	}

	std::vector<char> save(const registry& psf)
	{
		std::vector<def_table_t> indices; indices.reserve(psf.size());

		// Generate indices and calculate key table length
		std::size_t key_offset = 0, data_offset = 0;

		for (const auto& entry : psf)
		{
			def_table_t index;
			index.key_off = gsl::narrow<u32>(key_offset);
			index.param_fmt = entry.second.type;
			index.param_len = entry.second.size();
			index.param_max = entry.second.max_size;
			index.data_off = gsl::narrow<u32>(data_offset);

			// Update offsets:
			key_offset += gsl::narrow<u32>(entry.first.size() + 1); // key size
			data_offset += index.param_max;

			indices.push_back(index);
		}

		// Generate header
		header_t header;
		header.magic = *(u32*)"\0PSF";
		header.version = 0x101;
		header.off_key_table = gsl::narrow<u32>(sizeof(header_t) + sizeof(def_table_t) * psf.size());
		header.off_data_table = gsl::narrow<u32>(header.off_key_table + key_offset);
		header.entries_num = gsl::narrow<u32>(psf.size());

		// Save header and indices
		std::vector<char> result; result.reserve(header.off_data_table + data_offset);

		result.insert(result.end(), (char*)&header, (char*)&header + sizeof(header_t));
		result.insert(result.end(), (char*)indices.data(), (char*)indices.data() + sizeof(def_table_t) * psf.size());

		// Save key table
		for (const auto& entry : psf)
		{
			result.insert(result.end(), entry.first.begin(), entry.first.end());
			result.push_back('\0');
		}

		// Save data
		for (const auto& entry : psf)
		{
			const auto fmt = entry.second.type;
			const u32 max = entry.second.max_size;

			if (fmt == format::integer && max == sizeof(u32))
			{
				const le_t<u32> value = entry.second.as_integer();
				result.insert(result.end(), (char*)&value, (char*)&value + sizeof(u32));
			}
			else if (fmt == format::string || fmt == format::array)
			{
				const std::string& value = entry.second.as_string();
				const std::size_t size = std::min<std::size_t>(max, value.size());

				if (value.size() + (fmt == format::string) > size)
				{
					// TODO: check real limitations of PSF format
					log.error("psf::save(): value shrinkage (key='%s', value='%s', size=0x%zx, max=0x%x)", entry.first, value, size, max);
				}

				result.insert(result.end(), value.begin(), value.begin() + size);
				result.insert(result.end(), max - size, '\0'); // Write zeros up to max_size
			}
			else
			{
				log.error("psf::save(): invalid format (key='%s', fmt=0x%x)", entry.first, fmt);
			}
		}

		return result;
	}

	std::string get_string(const registry& psf, const std::string& key, const std::string& def)
	{
		const auto found = psf.find(key);

		if (found == psf.end() || (found->second.type != format::string && found->second.type != format::array))
		{
			return def;
		}

		return found->second.as_string();
	}

	// Get integer value or default value
	u32 get_integer(const registry& psf, const std::string& key, u32 def)
	{
		const auto found = psf.find(key);

		if (found == psf.end() || found->second.type != format::integer)
		{
			return def;
		}

		return found->second.as_integer();
	}
}
