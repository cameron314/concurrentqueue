#pragma once

struct DummyToken
{
	template<typename TQueue>
	DummyToken(TQueue const&)
	{
	}
};
