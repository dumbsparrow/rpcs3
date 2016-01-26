#pragma once

namespace psf
{
	enum class format : u16
	{
		array   = 0x0004, // claimed to be a non-NTS string (char array)
		string  = 0x0204,
		integer = 0x0404,
	};

	class entry final
	{
		u32 m_value_integer = 0; // TODO: is it really unsigned?
		std::string m_value_string;

	public:
		const format type; // Entry format (specified in the constructor)
		const u32 max_size; // Entry max size (supplementary info, stored in PSF format)

		// Construct string entry, assign the value
		entry(format type, u32 max_size, const std::string& value = {})
			: type(type)
			, max_size(max_size)
		{
			CHECK_ASSERTION(type == format::string || type == format::array);
			CHECK_ASSERTION(max_size);
			m_value_string = value;
		}

		// Construct integer entry, assign the value
		entry(u32 value)
			: type(format::integer)
			, max_size(sizeof(u32))
		{
			m_value_integer = value;
		}

		entry(const entry&) = default;
		entry(entry&&) = default;

		entry& operator =(const entry&) = default;
		entry& operator =(entry&&) = default;

		const std::string& as_string() const;
		u32 as_integer() const;

		entry& operator =(const std::string& value);
		entry& operator =(u32 value);

		u32 size() const;
	};

	// Define PSF registry as a sorted map of entries:
	using registry = std::map<std::string, entry>;

	// Load PSF registry from binary data
	registry load(const std::vector<char>&);

	// Convert PSF registry to binary format
	std::vector<char> save(const registry&);

	// Get string value or default value
	std::string get_string(const registry& psf, const std::string& key, const std::string& def = {});

	// Get integer value or default value
	u32 get_integer(const registry& psf, const std::string& key, u32 def = 0);
}
