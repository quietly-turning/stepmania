#ifndef SCREEN_OPTIONS_MASTER_H
#define SCREEN_OPTIONS_MASTER_H

#include "ScreenOptions.h"
#include "ModeChoice.h"

struct ConfOption;

class ScreenOptionsMaster: public ScreenOptions
{
public:
	ScreenOptionsMaster( CString sName );
	virtual ~ScreenOptionsMaster();

private:

	enum OptionRowType
	{
		ROW_LIST, /* list of custom settings */
		ROW_STEP, /* list of steps for the current song or course */
		ROW_CHARACTER, /* list of characters */
		ROW_CONFIG,
		NUM_OPTION_ROW_TYPES
	};

	struct OptionRowHandler
	{
		OptionRowType type;

		/* ROW_LIST: */
		vector<ModeChoice> ListEntries;
		ModeChoice Default;

		/* ROW_CONFIG: */
		const ConfOption *opt;
	};

	CString m_NextScreen;
	bool m_ForceSMOptionsNavigation;

	vector<OptionRowHandler> OptionRowHandlers;
	OptionRow *m_OptionRowAlloc;

	CString ConvertParamToThemeDifficulty( const CString &in ) const;
	int ExportOption( const OptionRow &row, const OptionRowHandler &hand, int pn, int sel );
	int ImportOption( const OptionRow &row, const OptionRowHandler &hand, int pn );
	void SetList( OptionRow &row, OptionRowHandler &hand, CString param, CString &TitleOut );
	void SetStep( OptionRow &row, OptionRowHandler &hand );
	void SetConf( OptionRow &row, OptionRowHandler &hand, CString param, CString &TitleOut );
	void SetCharacter( OptionRow &row, OptionRowHandler &hand );

protected:
	virtual void MenuStart( PlayerNumber pn );
	virtual void ImportOptions();
	virtual void ExportOptions();

	virtual void GoToNextState();
	virtual void GoToPrevState();
};


#endif
