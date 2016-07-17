
#ifndef __IM_TOOL_BAR_H__
#define __IM_TOOL_BAR_H__

#include "ImwConfig.h"

namespace ImWindow
{
//SFF_BEGIN
	class IMGUI_API ImwToolBar
	{
	public:
		ImwToolBar(int iHorizontalPriority = 0);
		ImwToolBar(const ImwToolBar& oToolBar);
		virtual						~ImwToolBar();

		void						Destroy();

		virtual void				OnToolBar() = 0;

		int							GetHorizontalPriority() const;
	private:
		int							m_iHorizontalPriority;
	};
	typedef ImwList<ImwToolBar*> ImwToolBarList;
//SFF_END
}


#endif // __IM_TOOL_BAR_H__