static void dEngine_ReadPackName(char* dst, int cap, const char* src)
{
	int i = 0;
	if (!dst || cap <= 0)
		return;
	while (*src && i < cap - 1)
	{
		if (*src == '_')
		{
			dst[i++] = ' ';
			src++;
		}
		else if (*src == '~' && src[1])
		{
			char c = src[1];
			int  v = (c >= '0' && c <= '9') ? c - '0'
			       : (c >= 'a' && c <= 'f') ? c - 'a' + 10
			       : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
			if (v > 0)			// 0 would terminate the string; ~0 is not a glyph
			{
				dst[i++] = (char)v;
				src += 2;
			}
			else
				dst[i++] = *src++;	// a lone '~' stays a '~'
		}
		else
			dst[i++] = *src++;
	}
	dst[i] = 0;
}
