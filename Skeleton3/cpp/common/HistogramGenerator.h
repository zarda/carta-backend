#pragma once
#include <qwt_plot.h>
#include <qwt_samples.h>
#include <qwt_plot_histogram.h>

class HistogramGenerator{	
public:
  typedef std::vector<std::pair<double,double> > ResultType;
  HistogramGenerator();
  void setData( ResultType data);
  void setStyle( QString style );
  void setLogScale(bool display);
  // void setColored( bool colored );
  QImage * toImage();
private:
	QwtPlot *m_plot;
	QwtPlotHistogram *m_histogram;
};
