﻿#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <algorithm>
#include <limits>
#include <sstream>

#include "aris_core.h"
#include "aris_dynamic_kernel.h"
#include "aris_dynamic_model.h"

namespace Aris
{
	namespace Dynamic
	{
		Interaction::Interaction(Object &father, const Aris::Core::XmlElement &xml_ele, int id)
			: Element(father, xml_ele, id)
		{
			if (!xml_ele.Attribute("PrtM"))throw std::runtime_error(std::string("xml element \"") + xml_ele.name() + "\" must have Attribute \"PrtM\"");
			try { model().partPool().find(xml_ele.Attribute("PrtM")); }
			catch (std::exception &) { throw std::runtime_error(std::string("can't find part M for element \"") + this->name() + "\""); }
			
			if (!xml_ele.Attribute("MakI"))throw std::runtime_error(std::string("xml element \"") + xml_ele.name() + "\" must have Attribute \"MakI\"");
			try { makI_ = model().partPool().find(xml_ele.Attribute("PrtM"))->markerPool().find(xml_ele.Attribute("MakI")); }
			catch (std::exception &) { throw std::runtime_error(std::string("can't find marker I for element \"") + this->name() + "\""); }

			if (!xml_ele.Attribute("PrtN"))throw std::runtime_error(std::string("xml element \"") + xml_ele.name() + "\" must have Attribute \"PrtN\"");
			try { model().partPool().find(xml_ele.Attribute("PrtN")); }
			catch (std::exception &) { throw std::runtime_error(std::string("can't find part N for element \"") + this->name() + "\""); }

			if (!xml_ele.Attribute("MakJ"))throw std::runtime_error(std::string("xml element \"") + xml_ele.name() + "\" must have Attribute \"MakJ\"");
			try { makJ_ = model().partPool().find(xml_ele.Attribute("PrtN"))->markerPool().find(xml_ele.Attribute("MakJ")); }
			catch (std::exception &) { std::runtime_error(std::string("can't find marker J for element \"") + this->name() + "\""); }
		}
		
		void Constraint::update()
		{
			double pm_M2N[4][4];
			double _tem_v1[6]{ 0 }, _tem_v2[6]{ 0 };

			/* Get pm M2N */
			s_pm_dot_pm(*makJ().fatherPart().invPm(), *makI().fatherPart().pm(), *pm_M2N);

			/*update CstMtx*/
			std::fill_n(this->csmJ(), this->dim() * 6, 0);
			s_tf_n(dim(), -1, *pm_M2N, this->csmI(), 0, this->csmJ());

			/*update CstAcc*/
			std::fill_n(this->csa(), this->dim(), 0);
			s_inv_tv(-1, *pm_M2N, makJ().fatherPart().prtVel(), 0, _tem_v1);
			s_cv(makI().fatherPart().prtVel(), _tem_v1, _tem_v2);
			s_dgemmTN(dim(), 1, 6, 1, csmI(), dim(), _tem_v2, 1, 0, csa(), 1);
		}
		void Constraint::saveXml(Aris::Core::XmlElement &xml_ele) const
		{
			xml_ele.DeleteChildren();
			xml_ele.SetName(this->name().data());

			Aris::Core::XmlElement *pActive = xml_ele.GetDocument()->NewElement("Active");
			if (this->isActive())
				pActive->SetText("True");
			else
				pActive->SetText("False");
			xml_ele.InsertEndChild(pActive);

			Aris::Core::XmlElement *pType = xml_ele.GetDocument()->NewElement("Type");
			pType->SetText(this->adamsTypeName().c_str());
			xml_ele.InsertEndChild(pType);

			Aris::Core::XmlElement *pPrtI = xml_ele.GetDocument()->NewElement("iPart");
			pPrtI->SetText(makI().fatherPart().name().data());
			xml_ele.InsertEndChild(pPrtI);

			Aris::Core::XmlElement *pPrtJ = xml_ele.GetDocument()->NewElement("jPart");
			pPrtJ->SetText(makJ().fatherPart().name().data());
			xml_ele.InsertEndChild(pPrtJ);

			Aris::Core::XmlElement *pMakI = xml_ele.GetDocument()->NewElement("iMarker");
			pMakI->SetText(makI().name().data());
			xml_ele.InsertEndChild(pMakI);

			Aris::Core::XmlElement *pMakJ = xml_ele.GetDocument()->NewElement("jMarker");
			pMakJ->SetText(makJ().name().data());
			xml_ele.InsertEndChild(pMakJ);
		}
		void Constraint::saveAdams(std::ofstream &file) const
		{
			file << "constraint create joint " << this->adamsTypeName() << "  &\r\n"
				<< "    joint_name = ." << model().name() << "." << this->name() << "  &\r\n"
				<< "    adams_id = " << adamsID() << "  &\r\n"
				<< "    i_marker_name = ." << model().name() << "." << this->makI().fatherPart().name() << "." << this->makI().name() << "  &\r\n"
				<< "    j_marker_name = ." << model().name() << "." << this->makJ().fatherPart().name() << "." << this->makJ().name() << "  \r\n"
				<< "!\r\n";
		}

		Marker::Marker(Object &father, const std::string &name, int id, const double *prt_pm, Marker *relative_mak, bool active)
			: Element(father, name, id, active)
		{
			static const double default_pm_in[16] = { 1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1 };
			prt_pm = prt_pm ? prt_pm : default_pm_in;

			if (relative_mak)
			{
				if (&relative_mak->fatherPart() != &fatherPart())
					throw std::logic_error("relative marker must has same father part with this marker");

				s_pm_dot_pm(*relative_mak->prtPm(), prt_pm, *prt_pm_);
			}
			else
			{
				std::copy_n(prt_pm, 16, static_cast<double *>(*prt_pm_));
			}
		}
		Marker::Marker(Object &father, const Aris::Core::XmlElement &xml_ele, int id)
			: Element(father, xml_ele, id)
		{
			double pm[16];

			if (!xml_ele.Attribute("Pos"))throw std::runtime_error(std::string("xml element \"") + xml_ele.name() + "\" must have Attribute \"Pos\"");
			try
			{
				Core::Matrix m = this->model().calculator.CalculateExpression(xml_ele.Attribute("Pos"));
				if (m.length() != 6)throw std::runtime_error("");
				s_pe2pm(m.data(), pm);
			}
			catch (std::exception &) { throw std::runtime_error(std::string("xml element \"") + this->name() + "\" attribute Pos must be a matrix expression"); }

			if (xml_ele.Attribute("RelativeTo") && (!xml_ele.Attribute("RelativeTo", "")))
			{
				try { s_pm_dot_pm(*fatherPart().markerPool().find(xml_ele.Attribute("RelativeTo"))->prtPm(), pm, *prt_pm_); }
				catch (std::exception &) { throw std::runtime_error(std::string("can't find relative marker for element \"") + this->name() + "\""); }
			}
			else
			{
				std::copy_n(pm, 16, static_cast<double*>(*prt_pm_));
			}
		}
		Marker::Marker(Part &prt, const double *prt_pe, const char* eul_type)
			:Marker(prt, "", 0)
		{
			static const double default_prt_pe[6]{ 0,0,0,0,0,0 };
			prt_pe = prt_pe ? prt_pe : default_prt_pe;
			s_pe2pm(prt_pe, *prt_pm_, eul_type);
		};
		auto Marker::fatherPart() const->const Part&{ return static_cast<const Part &>(this->father()); };
		auto Marker::fatherPart()->Part&{ return static_cast<Part &>(this->father()); };
		auto Marker::adamsID()const->std::size_t 
		{ 
			std::size_t id{ model().partPool().size() + this->id() + 1 };
			for (std::size_t i = 0; i < fatherPart().id(); ++i)
			{
				id += model().partPool().at(i).markerPool().size();
			}

			return id;
		};
		const double6& Marker::vel() const { return fatherPart().vel(); };
		const double6& Marker::acc() const { return fatherPart().acc(); };
		void Marker::update()
		{
			s_pm_dot_pm(*fatherPart().pm(), *prtPm(), *pm_);
		}
		void Marker::saveXml(Aris::Core::XmlElement &xml_ele) const
		{
			double value[10];

			xml_ele.DeleteChildren();
			xml_ele.SetName(this->name().data());

			Aris::Core::XmlElement *pPE = xml_ele.GetDocument()->NewElement("Pos");
			s_pm2pe(*prtPm(), value);
			pPE->SetText(Core::Matrix(1, 6, value).toString().c_str());
			xml_ele.InsertEndChild(pPE);

			Aris::Core::XmlElement *pRelativeMakEle = xml_ele.GetDocument()->NewElement("RelativeTo");
			pRelativeMakEle->SetText("");
			xml_ele.InsertEndChild(pRelativeMakEle);
		}
		
		Part::Part(Object &father, const std::string &name, int id, const double *im, const double *pm_in, const double *vel_in, const double *acc_in, bool active)
			: Marker(father, name, id, nullptr, nullptr, active), marker_pool_{ *this,"ChildMarker" }
		{
			if (im == nullptr)
			{
				std::fill_n(static_cast<double *>(*prt_im_), 36, 0);
				prt_im_[0][0] = 1;
				prt_im_[1][1] = 1;
				prt_im_[2][2] = 1;
				prt_im_[3][3] = 1;
				prt_im_[4][4] = 1;
				prt_im_[5][5] = 1;
			}
			else
			{
				std::copy_n(im, 36, *prt_im_);
			}

			if (pm_in == nullptr)
			{
				std::fill_n(static_cast<double *>(*this->pm()), 16, 0);
				this->pm_[0][0] = 1;
				this->pm_[1][1] = 1;
				this->pm_[2][2] = 1;
				this->pm_[3][3] = 1;
			}
			else
			{
				setPm(pm_in);
			}

			if (vel_in == nullptr)
			{
				std::fill_n(vel(), 6, 0);
			}
			else
			{
				setVel(vel_in);
			}

			if (acc_in == nullptr)
			{
				std::fill_n(acc(), 6, 0);
			}
			else
			{
				setAcc(acc_in);
			}
		}
		Part::Part(Object &father, const Aris::Core::XmlElement &xml_ele, int id)
			: Marker(father, xml_ele.name(), id), marker_pool_{ *this,"ChildMarker" }
		{
			Core::Matrix m;

			m = this->model().calculator.CalculateExpression(xml_ele.Attribute("Inertia"));
			s_gamma2im(m.data(), *prt_im_);

			m = this->model().calculator.CalculateExpression(xml_ele.Attribute("Pos"));
			s_pe2pm(m.data(), *pm());

			m = this->model().calculator.CalculateExpression(xml_ele.Attribute("Vel"));
			std::copy_n(m.data(), 6, vel());

			m = this->model().calculator.CalculateExpression(xml_ele.Attribute("Acc"));
			std::copy_n(m.data(), 6, acc());

			auto mak_group_xml = xml_ele.FirstChildElement("ChildMarker");
			for (auto ele = mak_group_xml->FirstChildElement(); ele != nullptr; ele = ele->NextSiblingElement())
			{
				marker_pool_.add(*ele).init();
			}

			if (xml_ele.Attribute("Graphic_File_Path") && (!xml_ele.Attribute("Graphic_File_Path", "")))
				graphic_file_path_ = xml_ele.Attribute("Graphic_File_Path");
		}
		void Part::update()
		{
			double tem[6];

			s_inv_pm(*pm(), *inv_pm_);
			s_tv(*inv_pm_, vel(), prt_vel_);
			s_tv(*inv_pm_, acc(), prt_acc_);
			s_tv(*inv_pm_, model().environment().gravity_, prt_gravity_);
			s_m6_dot_v6(*prt_im_, prt_gravity_, prt_fg_);
			s_m6_dot_v6(*prt_im_, prt_vel_, tem);
			s_cf(prt_vel_, tem, prt_fv_);
		}
		void Part::saveXml(Aris::Core::XmlElement &xml_ele) const
		{
			double value[10];
			
			xml_ele.DeleteChildren();
			xml_ele.SetName(this->name().data());

			Aris::Core::XmlElement *pActive = xml_ele.GetDocument()->NewElement("Active");
			if (this->isActive())
				pActive->SetText("True");
			else
				pActive->SetText("False");
			xml_ele.InsertEndChild(pActive);
			
			Aris::Core::XmlElement *pInertia = xml_ele.GetDocument()->NewElement("Inertia");
			s_im2gamma(*this->prtIm(),value);
			pInertia->SetText(Core::Matrix(1,10,value).toString().c_str());
			xml_ele.InsertEndChild(pInertia);

			Aris::Core::XmlElement *pPE = xml_ele.GetDocument()->NewElement("Pos");
			s_pm2pe(*this->pm(), value);
			pPE->SetText(Core::Matrix(1, 6, value).toString().c_str());
			xml_ele.InsertEndChild(pPE);

			Aris::Core::XmlElement *pVel = xml_ele.GetDocument()->NewElement("Vel");
			pVel->SetText(Core::Matrix(1, 6, vel()).toString().c_str());
			xml_ele.InsertEndChild(pVel);

			Aris::Core::XmlElement *pAcc = xml_ele.GetDocument()->NewElement("Acc");
			pAcc->SetText(Core::Matrix(1, 6, acc()).toString().c_str());
			xml_ele.InsertEndChild(pAcc);

			Aris::Core::XmlElement *pChildMak = xml_ele.GetDocument()->NewElement("ChildMarker");
			xml_ele.InsertEndChild(pChildMak);

			markerPool().saveXml(*pChildMak);

			Aris::Core::XmlElement *pGraphicFilePath = xml_ele.GetDocument()->NewElement("Graphic_File_Path");
			pGraphicFilePath->SetText(this->graphic_file_path_.c_str());
			xml_ele.InsertEndChild(pGraphicFilePath);
		}
		void Part::saveAdams(std::ofstream &file) const
		{
			if (this == model().ground_)
			{
				file << "!----------------------------------- ground -----------------------------------!\r\n"
					<< "!\r\n"
					<< "!\r\n"
					<< "! ****** Ground Part ******\r\n"
					<< "!\r\n"
					<< "defaults model  &\r\n"
					<< "    part_name = ground\r\n"
					<< "!\r\n"
					<< "defaults coordinate_system  &\r\n"
					<< "    default_coordinate_system = ." << model().name() << ".ground\r\n"
					<< "!\r\n"
					<< "! ****** Markers for current part ******\r\n"
					<< "!\r\n";
			}
			else
			{
				double pe[6];
				s_pm2pe(*this->pm(), pe, "313");
				Core::Matrix ori(1, 3, &pe[3]), loc(1, 3, &pe[0]);

				file << "!----------------------------------- " << this->name() << " -----------------------------------!\r\n"
					<< "!\r\n"
					<< "!\r\n"
					<< "defaults coordinate_system  &\r\n"
					<< "    default_coordinate_system = ." << model().name() << ".ground\r\n"
					<< "!\r\n"
					<< "part create rigid_body name_and_position  &\r\n"
					<< "    part_name = ." << model().name() << "." << this->name() << "  &\r\n"
					<< "    adams_id = " << this->adamsID() << "  &\r\n"
					<< "    location = (" << loc.toString() << ")  &\r\n"
					<< "    orientation = (" << ori.toString() << ")\r\n"
					<< "!\r\n"
					<< "defaults coordinate_system  &\r\n"
					<< "    default_coordinate_system = ." << model().name() << "." << this->name() << " \r\n"
					<< "!\r\n";

				
				double mass = this->prtIm()[0][0] == 0 ? 1 : prtIm()[0][0];
				std::fill_n(pe, 6, 0);
				pe[0] = this->prtIm()[1][5] / mass;
				pe[1] = -this->prtIm()[0][5] / mass;
				pe[2] = this->prtIm()[0][4] / mass;				

				file << "! ****** cm and mass for current part ******\r\n"
					<< "marker create  &\r\n"
					<< "    marker_name = ." << model().name() << "." << this->name() << ".cm  &\r\n"
					<< "    adams_id = " << this->adamsID() << "  &\r\n"
					<< "    location = ({" << pe[0] << "," << pe[1] << "," << pe[2] << "})  &\r\n"
					<< "    orientation = (" << "{0,0,0}" << ")\r\n"
					<< "!\r\n";

				double pm[16];
				double im[6][6];

				pe[0] = -pe[0];
				pe[1] = -pe[1];
				pe[2] = -pe[2];

				s_pe2pm(pe, pm);
				s_i2i(pm, *this->prtIm(), *im);

				/*！注意！*/
				//Adams里对惯量矩阵的定义貌似和我自己的定义在Ixy，Ixz，Iyz上互为相反数。别问我为什么，我也不知道。
				file << "part create rigid_body mass_properties  &\r\n"
					<< "    part_name = ." << model().name() << "." << this->name() << "  &\r\n"
					<< "    mass = " << this->prtIm()[0][0] << "  &\r\n"
					<< "    center_of_mass_marker = ." << model().name() << "." << this->name() << ".cm  &\r\n"
					<< "    inertia_marker = ." << model().name() << "." << this->name() << ".cm  &\r\n"
					<< "    ixx = " << im[3][3] << "  &\r\n"
					<< "    iyy = " << im[4][4] << "  &\r\n"
					<< "    izz = " << im[5][5] << "  &\r\n"
					<< "    ixy = " << -im[4][3] << "  &\r\n"
					<< "    izx = " << -im[5][3] << "  &\r\n"
					<< "    iyz = " << -im[5][4] << "\r\n"
					<< "!\r\n";

				
			}

			//导入marker
			for (auto &mak : this->markerPool())
			{
				double pe[6];

				s_pm2pe(*mak->prtPm(), pe, "313");
				Core::Matrix ori(1, 3, &pe[3]), loc(1, 3, &pe[0]);

				file << "marker create  &\r\n"
					<< "marker_name = ." << mak->model().name() << "." << mak->father().name() << "." << mak->name() << "  &\r\n"
					<< "adams_id = " << mak->adamsID() << "  &\r\n"
					<< "location = (" << loc.toString() << ")  &\r\n"
					<< "orientation = (" << ori.toString() << ")\r\n"
					<< "!\r\n";
			}
			//导入parasolid
			std::stringstream stream(this->graphic_file_path_);
			std::string path;
			while (stream >> path)
			{
				file << "file parasolid read &\r\n"
					<< "file_name = \"" << path << "\" &\r\n"
					<< "type = ASCII" << " &\r\n"
					<< "part_name = " << this->name() << " \r\n"
					<< "\r\n";
			}
		}

		Motion::Motion(Object &father, const std::string &name, int id, Marker &makI, Marker &makJ, const double *frc_coe, bool active)
			: Constraint(father, name, id, makI, makJ, active)
		{
			static const double default_frc_coe[3]{ 0,0,0 };

			frc_coe = frc_coe ? frc_coe : default_frc_coe;

			std::copy_n(frc_coe, 3, frc_coe_);
		}
		Motion::Motion(Object &father, const Aris::Core::XmlElement &xml_ele, int id)
			: Constraint(father, xml_ele, id)
		{
			if(!xml_ele.Attribute("FrcCoe"))throw std::runtime_error(std::string("xml element \"") + xml_ele.name() + "\" must have Attribute \"FrcCoe\"");
			try 
			{ 
				Core::Matrix m = model().calculator.CalculateExpression(xml_ele.Attribute("FrcCoe"));
				if (m.length() != 3)throw std::runtime_error("");
				std::copy_n(m.data(), 3, static_cast<double*>(frc_coe_));
			}
			catch (std::exception &) { throw std::runtime_error(std::string("xml element \"") + this->name() + "\" attribute FrcCoe must be a matrix expression"); }
		}
		void Motion::saveXml(Aris::Core::XmlElement &xml_ele) const
		{
			xml_ele.DeleteChildren();
			xml_ele.SetName(this->name().data());

			Aris::Core::XmlElement *pActive = xml_ele.GetDocument()->NewElement("Active");
			if (this->isActive())
				pActive->SetText("True");
			else
				pActive->SetText("False");
			xml_ele.InsertEndChild(pActive);

			Aris::Core::XmlElement *pType = xml_ele.GetDocument()->NewElement("Type");
			pType->SetText(this->adamsTypeName().c_str());
			xml_ele.InsertEndChild(pType);

			Aris::Core::XmlElement *pPrtI = xml_ele.GetDocument()->NewElement("iPart");
			pPrtI->SetText(makI().fatherPart().name().data());
			xml_ele.InsertEndChild(pPrtI);

			Aris::Core::XmlElement *pPrtJ = xml_ele.GetDocument()->NewElement("jPart");
			pPrtJ->SetText(makJ().fatherPart().name().data());
			xml_ele.InsertEndChild(pPrtJ);

			Aris::Core::XmlElement *pMakI = xml_ele.GetDocument()->NewElement("iMarker");
			pMakI->SetText(makI().name().data());
			xml_ele.InsertEndChild(pMakI);

			Aris::Core::XmlElement *pMakJ = xml_ele.GetDocument()->NewElement("jMarker");
			pMakJ->SetText(makJ().name().data());
			xml_ele.InsertEndChild(pMakJ);

			Aris::Core::XmlElement *pFrictionCoefficients = xml_ele.GetDocument()->NewElement("Friction_Coefficients");

			pFrictionCoefficients->SetText(Core::Matrix(1, 3, frcCoe()).toString().c_str());
			xml_ele.InsertEndChild(pFrictionCoefficients);
		}

		void Environment::saveXml(Aris::Core::XmlElement &xml_ele) const
		{
			xml_ele.DeleteChildren();
			xml_ele.SetName("Enviroment");

			Aris::Core::XmlElement *pGravity = xml_ele.GetDocument()->NewElement("Gravity");
			pGravity->SetText(Core::Matrix(1, 6, gravity_).toString().c_str());
			xml_ele.InsertEndChild(pGravity);
		}
		void Environment::fromXmlElement(const Aris::Core::XmlElement &xml_ele)
		{
			Core::Matrix m = model().calculator.CalculateExpression(xml_ele.FirstChildElement("Gravity")->GetText());
			memcpy(gravity_, m.data(), sizeof(gravity_));
		}
		void Environment::saveAdams(std::ofstream &file) const
		{
			file << "!-------------------------- Default Units for Model ---------------------------!\r\n"
				<< "!\r\n"
				<< "!\r\n"
				<< "defaults units  &\r\n"
				<< "    length = meter  &\r\n"
				<< "    angle = rad  &\r\n"
				<< "    force = newton  &\r\n"
				<< "    mass = kg  &\r\n"
				<< "    time = sec\r\n"
				<< "!\n"
				<< "defaults units  &\r\n"
				<< "    coordinate_system_type = cartesian  &\r\n"
				<< "    orientation_type = body313\r\n"
				<< "!\r\n"
				<< "!------------------------ Default Attributes for Model ------------------------!\r\n"
				<< "!\r\n"
				<< "!\r\n"
				<< "defaults attributes  &\r\n"
				<< "    inheritance = bottom_up  &\r\n"
				<< "    icon_visibility = off  &\r\n"
				<< "    grid_visibility = off  &\r\n"
				<< "    size_of_icons = 5.0E-002  &\r\n"
				<< "    spacing_for_grid = 1.0\r\n"
				<< "!\r\n"
				<< "!------------------------------ Adams/View Model ------------------------------!\r\n"
				<< "!\r\n"
				<< "!\r\n"
				<< "model create  &\r\n"
				<< "   model_name = " << this->model().name() << "\r\n"
				<< "!\r\n"
				<< "view erase\r\n"
				<< "!\r\n"
				<< "!---------------------------------- Accgrav -----------------------------------!\r\n"
				<< "!\r\n"
				<< "!\r\n"
				<< "force create body gravitational  &\r\n"
				<< "    gravity_field_name = gravity  &\r\n"
				<< "    x_component_gravity = " << this->gravity_[0] << "  &\r\n"
				<< "    y_component_gravity = " << this->gravity_[1] << "  &\r\n"
				<< "    z_component_gravity = " << this->gravity_[2] << "\r\n"
				<< "!\r\n";
		};

		class Script::Imp 
		{
		public:
			struct Node
			{
				virtual void DoNode() {};
				virtual void update() {};
				virtual std::uint32_t MsConsumed()const { return 0; };
				virtual std::string AdamsScript()const = 0;
			};
			struct ActivateNode final :public Node
			{
				virtual void DoNode()override { pEle->activate(isActive); };
				virtual std::string AdamsScript()const override
				{
					std::stringstream ss;
					std::string cmd = isActive ? "activate/" : "deactivate/";
					ss << cmd << pEle->adamsGroupName() << ", id=" << pEle->adamsID();
					return std::move(ss.str());
				};
				
				explicit ActivateNode(Element &ele, bool isActive) :pEle(&ele), isActive(isActive) {};
				bool isActive;
				Element *pEle;
			};
			struct MoveMarkerNode final :public Node
			{
				virtual void DoNode()override 
				{
					s_pm2pe(prtPe, *mak_move_.prt_pm_);
				};
				void update() override 
				{
					double pm_target_g[16];

					s_pm_dot_pm(*mak_target_.fatherPart().pm(), *mak_target_.prtPm(), pm_target_g);
					s_inv_pm_dot_pm(*mak_move_.fatherPart().pm(), pm_target_g, &mak_move_.prt_pm_[0][0]);
					s_pm2pe(*mak_move_.prt_pm_, prtPe);
				};
				virtual std::string AdamsScript()const override
				{
					std::stringstream ss;
					ss << "marker/" << mak_move_.adamsID()
						<< " , QP = " << prtPe[0] << "," << prtPe[1] << "," << prtPe[2]
						<< " , REULER =" << prtPe[3] << "," << prtPe[4] << "," << prtPe[5];
					return std::move(ss.str());
				};
				
				explicit MoveMarkerNode(Marker &mak_move, const Marker &mak_target) :mak_move_(mak_move), mak_target_(mak_target){};
				Marker &mak_move_;
				const Marker &mak_target_;
				double prtPe[6];
			};
			struct SimulateNode final :public Node
			{
				virtual std::uint32_t MsConsumed()const { return ms_dur_; };
				virtual std::string AdamsScript()const override
				{
					std::stringstream ss;
					ss << "simulate/transient, dur=" << double(ms_dur_) / 1000.0 << ", dtout=" << double(ms_dt_) / 1000.0;
					return std::move(ss.str());
				};
				
				explicit SimulateNode(std::uint32_t ms_dur, std::uint32_t ms_dt) :ms_dur_(ms_dur), ms_dt_(ms_dt) { };
				std::uint32_t ms_dur_;
				std::uint32_t ms_dt_;
			};

			void activate(Element &ele, bool isActive)
			{
				node_list_.push_back(std::unique_ptr<Node>(new ActivateNode(ele,isActive)));
			}
			void alignMarker(Marker &mak, const Marker& mak_target)
			{
				node_list_.push_back(std::unique_ptr<Node>(new MoveMarkerNode(mak, mak_target)));
			}
			void simulate(std::uint32_t ms_dur, std::uint32_t ms_dt)
			{
				node_list_.push_back(std::unique_ptr<Node>(new SimulateNode(ms_dur, ms_dt)));
			}
			void saveAdams(std::ofstream &file) const
			{
				file << "simulation script create &\r\n"
					<< "sim_script_name = default_script &\r\n"
					<< "solver_commands = ";
				
				for (auto &pNode : node_list_)
				{
					file <<"&\r\n\""<< pNode->AdamsScript() << "\",";
				}

				file << "\"\"\r\n\r\n";
			}
			std::int32_t endTime()const
			{
				std::uint32_t end_time{0};

				for (auto& node : node_list_)end_time += node->MsConsumed();
				
				return end_time;
			}
			void setTopologyAt(std::uint32_t ms_time) 
			{
				std::uint32_t now{ 0 };
				for (auto p = node_list_.begin(); (p != node_list_.end()) && (now + (*p)->MsConsumed() <= ms_time); ++p)
				{
					(*p)->DoNode();
				}
			};

		private:
			std::list<std::unique_ptr<Node> > node_list_;

			friend Script;
		};
		Script::Script() :pImp(new Imp) {};
		Script::~Script() {};
		void Script::activate(Element &ele, bool isActive) { pImp->activate(ele, isActive); };
		void Script::alignMarker(Marker &mak, const Marker& mak_target) { pImp->alignMarker(mak, mak_target); };
		void Script::simulate(std::uint32_t ms_dur, std::uint32_t ms_dt) { pImp->simulate(ms_dur, ms_dt); };
		void Script::saveAdams(std::ofstream &file) const
		{
			pImp->saveAdams(file);
		}
		bool Script::empty()const { return pImp->node_list_.empty(); };
		std::uint32_t Script::endTime()const 
		{
			return pImp->endTime();
		};
		void Script::setTopologyAt(std::uint32_t ms_time) { pImp->setTopologyAt(ms_time); };
		void Script::updateAt(std::uint32_t ms_time)
		{
			std::uint32_t now = 0;
			for (auto&node : pImp->node_list_)
			{
				now += node->MsConsumed();
				if (now == ms_time)node->update();
			}
		}
		void Script::clear() { pImp->node_list_.clear(); };

		Model::Model(const std::string & name)
			: Object(*this, name)
			, environment_(*this)
			, ground_(nullptr)
		{
			partPool().add<Part>("Ground");
			ground_ = partPool().find("Ground");
		}
		Model::~Model()
		{
		}

		void Model::dynSetSolveMethod(std::function<void(int dim, const double *D, const double *b, double *x)> solve_method)
		{
			this->dyn_solve_method_ = solve_method;
		}
		void Model::dynCstMtx(double *cst_mtx) const
		{
			std::fill_n(cst_mtx, dynDimN()*dynDimM(), 0);

			for (int i = 0; i < 6; ++i)
			{
				cst_mtx[dynDimN()*(ground_->row_id_ + i) + i] = 1;
			}

			for (auto &jnt : joint_pool_)
			{
				if (jnt->isActive())
				{
					s_block_cpy(6, jnt->dim(), jnt->csmI(), 0, 0, jnt->dim(), cst_mtx, jnt->makI().fatherPart().row_id_, jnt->col_id_, dynDimN());
					s_block_cpy(6, jnt->dim(), jnt->csmJ(), 0, 0, jnt->dim(), cst_mtx, jnt->makJ().fatherPart().row_id_, jnt->col_id_, dynDimN());
				}
			}
			for (auto &mot : motion_pool_)
			{
				if (mot->isActive())
				{
					s_block_cpy(6, 1, mot->csmI(), 0, 0, 1, cst_mtx, mot->makI().fatherPart().row_id_, mot->col_id_, dynDimN());
					s_block_cpy(6, 1, mot->csmJ(), 0, 0, 1, cst_mtx, mot->makJ().fatherPart().row_id_, mot->col_id_, dynDimN());
				}
			}
		}
		void Model::dynIneMtx(double *ine_mtx) const
		{
			std::fill_n(ine_mtx, dynDimM()*dynDimM(), 0);

			for (int i = 0; i < 6; ++i)
			{
				ine_mtx[dynDimM()*(ground_->row_id_ + i) + ground_->row_id_ + i] = 1;
			}

			for (auto &prt : part_pool_)
			{
				if (prt->isActive())
				{
					s_block_cpy(6, 6, *(prt->prt_im_), 0, 0, 6, ine_mtx, prt->row_id_, prt->row_id_, dynDimM());
				}
			}
		}
		void Model::dynCstAcc(double *cst_acc) const
		{
			std::fill_n(cst_acc, dynDimN(), 0);

			for (auto &jnt : joint_pool_)
			{
				if (jnt->isActive())
				{
					std::copy_n(jnt->csa(), jnt->dim(), &cst_acc[jnt->col_id_]);
				}
			}
			for (auto &mot : motion_pool_)
			{
				if (mot->isActive())
				{
					cst_acc[mot->col_id_] = *mot->csa();
				}
			}
		}
		void Model::dynPrtFce(double *prt_fce) const
		{
			std::fill_n(prt_fce, dynDimM(), 0);

			for (auto &prt : part_pool_)
			{
				if (prt->isActive())
				{
					s_daxpy(6, -1, prt->prt_fg_, 1, &prt_fce[prt->row_id_], 1);
					s_daxpy(6, 1, prt->prt_fv_, 1, &prt_fce[prt->row_id_], 1);
				}
			}

			for (auto &fce : force_pool_)
			{
				if (fce->isActive())
				{
					s_daxpy(6, -1, fce->fceI(), 1, &prt_fce[fce->makI().fatherPart().row_id_], 1);
					s_daxpy(6, -1, fce->fceJ(), 1, &prt_fce[fce->makJ().fatherPart().row_id_], 1);
				}
			}
		}
		void Model::dynCstFce(double *cst_fce) const
		{
			for (auto &jnt : joint_pool_)
			{
				if (jnt->isActive())
				{
					std::copy_n(jnt->csf(), jnt->dim(), &cst_fce[jnt->col_id_]);
				}
			}
			for (auto &mot : motion_pool_)
			{
				if (mot->isActive())
				{
					cst_fce[mot->col_id_] = mot->motFceDyn();
				}
			}

		}
		void Model::dynPrtAcc(double *cst_acc) const
		{
			for (auto &prt : part_pool_)
			{
				if (prt->isActive())
				{
					std::copy_n(prt->prtAcc(), 6, &cst_acc[prt->row_id_]);
				}
			}
		}
		void Model::dynPre()
		{
			int pid = 0;//part id
			int cid = 6;//Joint id

			for (auto &part:part_pool_)
			{
				if (part->isActive())
				{
					part->row_id_ = pid;
					pid += 6;
				}
				else
				{
					part->row_id_ = 0;
				}
			}
			for (auto &joint:joint_pool_)
			{
				if (joint->isActive())
				{
					joint->init();
					joint->col_id_ = cid;
					cid += joint->dim();
				}
				else
				{
					joint->col_id_ = 0;
				}
			}
			for (auto &motion:motion_pool_)
			{
				if (motion->isActive())
				{
					motion->col_id_ = cid;
					cid++;
					motion->init();
				}
				else
				{
					motion->col_id_ = 0;
					motion->init();
				}
			}

			dyn_prt_dim_ = pid;
			dyn_cst_dim_ = cid;
		}
		void Model::dynUpd()
		{
			for (auto &prt : part_pool_)
			{
				if (prt->isActive())prt->update();
			}
			for (auto &jnt : joint_pool_)
			{
				if (jnt->isActive())jnt->update();
			}
			for (auto &mot : motion_pool_)
			{
				if (mot->isActive())mot->update();
				//std::cout << *mot->csa() << std::endl;
			}
			for (auto &fce : force_pool_)
			{
				if (fce->isActive())fce->update();
			}
		}
		void Model::dynMtx(double *D, double *b) const
		{
			dynCstMtx(&D[(dynDim())*dynDimM()]);
			s_block_cpy(dynDimM(), dynDimN(), &D[(dynDim())*dynDimM()], 0, 0, dynDimN(), D, 0, dynDimM(), dynDim());
			
			dynIneMtx(&D[(dynDim())*dynDimM()]);
			s_block_cpy(dynDimM(), dynDimM(), -1, &D[(dynDim())*dynDimM()], 0, 0, dynDimM(), 0, D, 0, 0, dynDim());

			std::fill_n(&D[(dynDim())*dynDimM()], dynDimN()*(dynDim()), 0);
			s_block_cpyT(dynDimM(), dynDimN(), D, 0, dynDimM(), dynDim(), D, dynDimM(), 0, dynDim());

			dynPrtFce(b);
			dynCstAcc(b + dynDimM());
		}
		void Model::dynSov(const double *D, const double *b, double *x) const
		{
			if (dyn_solve_method_)
			{
				dyn_solve_method_(dynDim(), D, b, x);
			}
			else
			{
				throw std::runtime_error("please set solve_method before use DynSov");
			}
		}
		void Model::dynEnd(const double *x)
		{
			for (auto &prt : part_pool_)
			{
				if (prt->isActive())
				{
					std::copy_n(&x[prt->row_id_], 6, prt->prt_acc_);
				}
			}
			for (auto &jnt : joint_pool_)
			{
				if (jnt->isActive())
				{
					std::copy_n(&x[jnt->col_id_ + dynDimM()], jnt->dim(), jnt->csf());
				}
			}
			for (auto &mot : motion_pool_)
			{
				if (mot->isActive())
				{
					mot->mot_fce_dyn_ = x[mot->col_id_ + dynDimM()];
				}
			}
		}
		void Model::dynUkn(double *x) const
		{
			this->dynPrtAcc(x);
			this->dynCstFce(x + dynDimM());
		}
		void Model::dyn()
		{
			/*
			DynPre();
			
			std::vector<double> C(dynDimM() * dynDimN());
			std::vector<double> a_c(dynDimN());
			std::vector<double> I_mat(dynDimM() * dynDimM());
			std::vector<double> f(dynDimM());
			std::vector<double> D((dynDim()) * (dynDim()));
			std::vector<double> b(dynDim());

			DynMtx(C.data(), a_c.data(), I_mat.data(), f.data(), D.data(), b.data());

			std::vector<double> x(dynDim());
			solveMethod(dynDim(), D.data(), b.data(), x.data());

			DynEnd(x.data());
			*/
			dynPre();
			std::vector<double> D(dynDim() * dynDim());
			std::vector<double> b(dynDim());
			std::vector<double> x(dynDim());
			dynUpd();
			dynMtx(D.data(), b.data());
			dynSov(D.data(), b.data(), x.data());
			dynEnd(x.data());
		}

		void Model::clbSetInverseMethod(std::function<void(int n, double *A)> inverse_method)
		{
			this->clb_inverse_method_ = inverse_method;
		}
		void Model::clbPre()
		{
			dynPre();

			if (dynDimN() != dynDimM())
			{
				throw std::logic_error("must calibrate square matrix");
			}

			clb_dim_m_ = 0;
			clb_dim_n_ = 0;
			clb_dim_gam_ = 0;
			clb_dim_frc_ = 0;

			for (auto &i : motion_pool_)
			{
				if (i->isActive())
				{
					clb_dim_m_++;
					clb_dim_frc_ += 3;
					clb_dim_n_ += 3;
				}
			}
			for (auto &i : part_pool_)
			{
				if (i->isActive())
				{
					clb_dim_n_ += 10;
					clb_dim_gam_ += 10;
				}
			}

		}
		void Model::clbUpd()
		{
			dynUpd();
		}
		void Model::clbMtx(double *clb_D, double *clb_b)const
		{
			if (!clb_inverse_method_)throw std::runtime_error("please set inverse method before calibrate");
			if (dynDimN() != dynDimM()) throw std::logic_error("must calibrate square matrix");

			/*初始化*/
			static Core::Matrix clb_d_m, clb_b_m;

			clb_d_m.resize(clbDimM(), clbDimN());
			clb_b_m.resize(clbDimM(), 1);

			memset(clb_d_m.data(), 0, clb_d_m.length() * sizeof(double));
			memset(clb_b_m.data(), 0, clb_b_m.length() * sizeof(double));

			/*求A，即C的逆*/
			Core::Matrix A(dynDimM(), dynDimM()), B(dynDimM(), dynDimM());

			std::vector<double> C(dynDimM() * dynDimM());
			std::vector<double> f(dynDimM());

			dynCstMtx(C.data());
			std::copy(C.begin(), C.end(), A.data());
			clb_inverse_method_(dynDimM(), A.data());

			/*求B*/
			const int beginRow = dynDimM() - clbDimM();

			for (auto &i:part_pool_)
			{
				if (i->isActive())
				{
					double cm[6][6];
					s_cmf(i->prtVel(), *cm);
					s_dgemm(clbDimM(), 6, 6, 1, &A(beginRow,i->row_id_), dynDimM(), *cm, 6, 0, &B(beginRow, i->row_id_), dynDimM());
				}
			}

			/*求解clb_d*/
			int col1 = 0, col2 = 0;

			for (auto &i:part_pool_)
			{
				if (i->isActive())
				{
					double q[6]{0};
					std::copy_n(i->prtAcc(), 6, q);
					s_daxpy(6, -1, i->prtGravity(), 1, q, 1);
					
					double v[6];
					std::copy_n(i->prtVel(), 6, v);

					for (std::size_t j = 0; j < clbDimM(); ++j)
					{
						clb_d_m(j, col1) = A(beginRow + j, col2 + 0) * q[0] + A(beginRow + j, col2 + 1) * q[1] + A(beginRow + j, col2 + 2) * q[2];
						clb_d_m(j, col1 + 1) = A(beginRow + j, col2 + 1) * q[5] + A(beginRow + j, col2 + 5) * q[1] - A(beginRow + j, col2 + 2) * q[4] - A(beginRow + j, col2 + 4) * q[2];
						clb_d_m(j, col1 + 2) = A(beginRow + j, col2 + 2) * q[3] + A(beginRow + j, col2 + 3) * q[2] - A(beginRow + j, col2 + 0) * q[5] - A(beginRow + j, col2 + 5) * q[0];
						clb_d_m(j, col1 + 3) = A(beginRow + j, col2 + 0) * q[4] + A(beginRow + j, col2 + 4) * q[0] - A(beginRow + j, col2 + 1) * q[3] - A(beginRow + j, col2 + 3) * q[1];
						clb_d_m(j, col1 + 4) = A(beginRow + j, col2 + 3) * q[3];
						clb_d_m(j, col1 + 5) = A(beginRow + j, col2 + 4) * q[4];
						clb_d_m(j, col1 + 6) = A(beginRow + j, col2 + 5) * q[5];
						clb_d_m(j, col1 + 7) = A(beginRow + j, col2 + 3) * q[4] + A(beginRow + j, col2 + 4) * q[3];
						clb_d_m(j, col1 + 8) = A(beginRow + j, col2 + 3) * q[5] + A(beginRow + j, col2 + 5) * q[3];
						clb_d_m(j, col1 + 9) = A(beginRow + j, col2 + 4) * q[5] + A(beginRow + j, col2 + 5) * q[4];

						clb_d_m(j, col1) += B(beginRow + j, col2 + 0) * v[0] + B(beginRow + j, col2 + 1) * v[1] + B(beginRow + j, col2 + 2) * v[2];
						clb_d_m(j, col1 + 1) += B(beginRow + j, col2 + 1) * v[5] + B(beginRow + j, col2 + 5) * v[1] - B(beginRow + j, col2 + 2) * v[4] - B(beginRow + j, col2 + 4) * v[2];
						clb_d_m(j, col1 + 2) += B(beginRow + j, col2 + 2) * v[3] + B(beginRow + j, col2 + 3) * v[2] - B(beginRow + j, col2 + 0) * v[5] - B(beginRow + j, col2 + 5) * v[0];
						clb_d_m(j, col1 + 3) += B(beginRow + j, col2 + 0) * v[4] + B(beginRow + j, col2 + 4) * v[0] - B(beginRow + j, col2 + 1) * v[3] - B(beginRow + j, col2 + 3) * v[1];
						clb_d_m(j, col1 + 4) += B(beginRow + j, col2 + 3) * v[3];
						clb_d_m(j, col1 + 5) += B(beginRow + j, col2 + 4) * v[4];
						clb_d_m(j, col1 + 6) += B(beginRow + j, col2 + 5) * v[5];
						clb_d_m(j, col1 + 7) += B(beginRow + j, col2 + 3) * v[4] + B(beginRow + j, col2 + 4) * v[3];
						clb_d_m(j, col1 + 8) += B(beginRow + j, col2 + 3) * v[5] + B(beginRow + j, col2 + 5) * v[3];
						clb_d_m(j, col1 + 9) += B(beginRow + j, col2 + 4) * v[5] + B(beginRow + j, col2 + 5) * v[4];
					}
					col1 += 10;
					col2 += 6;
				}
			}

			/*求解clb_b*/
			std::fill(f.begin(), f.end(), 0);
			int row = 0;
			for (auto &mot : motion_pool_)
			{
				if (mot->isActive())
				{
					clb_b_m(row, 0) = mot->mot_fce_;
					++row;
				}
			}
			for (auto &fce : force_pool_)
			{
				if (fce->isActive())
				{
					s_daxpy(6, 1, fce->fceI(), 1, &f[fce->makI().fatherPart().row_id_], 1);
					s_daxpy(6, 1, fce->fceJ(), 1, &f[fce->makJ().fatherPart().row_id_], 1);
				}
			}
			s_dgemm(clbDimM(), 1, dynDimM(), 1, &A(beginRow,0), dynDimM(), f.data(), 1, 1, clb_b_m.data(), 1);

			/*以下添加驱动摩擦系数*/
			row = 0;
			for (auto &mot : motion_pool_)
			{
				//默认未激活的motion处于力控模式
				if (mot->isActive())
				{
					clb_d_m(row, clbDimGam() + row * 3) += s_sgn(mot->motVel());
					clb_d_m(row, clbDimGam() + row * 3 + 1) += mot->motVel();
					clb_d_m(row, clbDimGam() + row * 3 + 2) += mot->motAcc();
					++row;
				}
			}

			std::copy_n(clb_d_m.data(), clb_d_m.length(), clb_D);
			std::copy_n(clb_b_m.data(), clb_b_m.length(), clb_b);
		}
		void Model::clbUkn(double *clb_x)const
		{
			int row = 0;
			for (auto &prt : part_pool_)
			{
				if (prt->isActive())
				{
					s_im2gamma(*prt->prtIm(), clb_x + row);
					row += 10;
				}
			}

			for (auto &mot : motion_pool_)
			{
				if (mot->isActive())
				{
					std::copy_n(mot->frcCoe(), 3, clb_x + row);
					row += 3;
				}
			}
		}
		
		void Model::simKin(const PlanFunc &func, const PlanParamBase &param, SimResult &result, bool using_script)
		{
			//储存起始的每个杆件的位置
			result.begin_prt_pe_.clear();
			for (std::size_t i = 0; i < partPool().size(); ++i)
			{
				std::array<double, 6>pe;
				partPool().at(i).getPe(pe.data());
				result.begin_prt_pe_.push_back(pe);
			}

			//初始化变量
			result.Pin_.clear();
			result.Pin_.resize(motionPool().size());

			//起始位置
			result.time_.push_back(0);
			for (std::size_t i = 0; i < motionPool().size(); ++i)
			{
				motionPool().at(i).update();
				result.Pin_.at(i).push_back(motionPool().at(i).motPos());
			}

			if (using_script)
			{
				this->script().updateAt(0);
				this->script().setTopologyAt(0);
			}

			//其他位置
			for (param.count = 0; true; ++param.count)
			{
				auto is_sim = func(*this, param);

				result.time_.push_back(param.count + 1);
				for (std::size_t i = 0; i < motionPool().size(); ++i)
				{
					result.Pin_.at(i).push_back(motionPool().at(i).motPos());
				}

				if (using_script)
				{
					this->script().updateAt(param.count + 1);
					this->script().setTopologyAt(param.count + 1);
				}

				if (!is_sim)break;
			}

			//恢复起始位置
			auto pe = result.begin_prt_pe_.begin();
			for (std::size_t i = 0; i < partPool().size(); ++i)
			{
				partPool().at(i).setPe(pe->data());
				++pe;
			}
		}
		void Model::simDyn(const PlanFunc &func, const PlanParamBase &param, SimResult &result, bool using_script)
		{
			
		}
		void Model::simKinAkima(const PlanFunc &func, const PlanParamBase &param, SimResult &result, int akima_interval, bool using_script)
		{
			simKin(func, param, result, using_script);
			
			//生成Akima
			std::list<double> time_akima_data;
			std::vector<std::list<double> > pos_akima_data(motionPool().size());

			//初始化迭代器
			auto p_time = result.time_.begin();
			std::vector<std::list<double>::iterator> p_Pin(motionPool().size());
			for (std::size_t i = 0; i < motionPool().size(); ++i)
				p_Pin.at(i) = result.Pin_.at(i).begin();

			//生成Akima
			for (std::size_t i = 0; i < result.time_.size(); ++i)
			{
				if ((i == (result.time_.size() - 1)) || (i%akima_interval == 0))
				{
					time_akima_data.push_back(*p_time / 1000.0);

					for (std::size_t j = 0; j < motionPool().size(); ++j)
					{
						pos_akima_data.at(j).push_back(*p_Pin.at(j));
					}
				}

				++p_time;
				for (auto &j : p_Pin)++j;
			}

			//设置Akima
			for (std::size_t i = 0; i < motionPool().size(); ++i)
			{
				motionPool().at(i).posCurve.reset(new Akima(time_akima_data, pos_akima_data.at(i)));
			}
		}
		void Model::simDynAkima(const PlanFunc &func, const PlanParamBase &param, SimResult &result, int akima_interval, bool using_script)
		{
			simKinAkima(func, param, result, akima_interval, using_script);
			
			result.Pin_.clear();
			result.Fin_.clear();
			result.Ain_.clear();
			result.Vin_.clear();

			result.Pin_.resize(motionPool().size());
			result.Fin_.resize(motionPool().size());
			result.Vin_.resize(motionPool().size());
			result.Ain_.resize(motionPool().size());

			//仿真计算
			for (std::size_t t = 0; t < result.time_.size();++t)
			{
				std::cout << t << std::endl;
				
				if (using_script)
				{
					this->script().setTopologyAt(t);
				}
				for (std::size_t j = 0; j < motionPool().size(); ++j)
				{
					motionPool().at(j).mot_pos_ = motionPool().at(j).posAkima(t / 1000.0, '0');
				}
				kinFromPin();
				for (std::size_t j = 0; j < motionPool().size(); ++j)
				{
					motionPool().at(j).mot_vel_ = motionPool().at(j).posAkima(t / 1000.0, '1');
				}
				kinFromVin();
				for (std::size_t j = 0; j < motionPool().size(); ++j)
				{
					motionPool().at(j).mot_acc_ = motionPool().at(j).posAkima(t / 1000.0, '2');
				}
				dyn();
				for (std::size_t j = 0; j < motionPool().size(); ++j)
				{
					result.Fin_.at(j).push_back(motionPool().at(j).mot_fce_dyn_);
					result.Pin_.at(j).push_back(motionPool().at(j).motPos());
					result.Vin_.at(j).push_back(motionPool().at(j).motVel());
					result.Ain_.at(j).push_back(motionPool().at(j).motAcc());
				}
			}

			//恢复起始位置
			auto pe = result.begin_prt_pe_.begin();
			for (std::size_t i = 0; i < partPool().size(); ++i)
			{
				partPool().at(i).setPe(pe->data());
				++pe;
			}
		}
		void Model::simToAdams(const std::string &adams_file, const PlanFunc &fun, const PlanParamBase &param, int ms_dt, bool using_script)
		{
			SimResult result;
			simKinAkima(fun, param, result, ms_dt, using_script);
			this->saveAdams(adams_file, using_script);
		}

		void Model::loadXml(const std::string &filename)
		{
			Aris::Core::XmlDocument xmlDoc;
			
			if (xmlDoc.LoadFile(filename.c_str()) != 0)
			{
				throw std::logic_error((std::string("could not open file:") + std::string(filename)));
			}

			loadXml(xmlDoc);
		}
		void Model::loadXml(const Aris::Core::XmlDocument &xml_doc)
		{
			auto pModel = xml_doc.RootElement()->FirstChildElement("Model");

			if (!pModel)throw std::logic_error("can't find Model element in xml file");

			loadXml(*pModel);
		}
		void Model::loadXml(const Aris::Core::XmlElement &xml_ele)
		{
			auto pVar = xml_ele.FirstChildElement("Variable");
			if (!pVar)throw(std::logic_error("Model must have variable element"));
			auto pEnv = xml_ele.FirstChildElement("Environment");
			if (!pEnv)throw(std::logic_error("Model must have environment element"));
			auto pPrt = xml_ele.FirstChildElement("Part");
			if (!pPrt)throw(std::logic_error("Model must have part element"));
			auto pJnt = xml_ele.FirstChildElement("Joint");
			if (!pJnt)throw(std::logic_error("Model must have joint element"));
			auto pMot = xml_ele.FirstChildElement("Motion");
			if (!pMot)throw(std::logic_error("Model must have motion element"));
			auto pFce = xml_ele.FirstChildElement("Force");
			if (!pFce)throw(std::logic_error("Model must have force element"));

			calculator.ClearVariables();
			for (auto ele = pVar->FirstChildElement(); ele != nullptr; ele = ele->NextSiblingElement())
			{
				calculator.AddVariable(ele->name(), calculator.CalculateExpression(ele->GetText()));
			}

			environment().fromXmlElement(*pEnv);

			part_pool_.clear();
			joint_pool_.clear();
			motion_pool_.clear();
			force_pool_.clear();

			part_pool_ = std::move(ElementPool<Part>(*this, *pPrt));		
			joint_pool_ = std::move(ElementPool<Joint>(*this, *pJnt));
			motion_pool_ = std::move(ElementPool<Motion>(*this, *pMot));
			force_pool_ = std::move(ElementPool<Force>(*this, *pFce));

			if(!(ground_ = part_pool_.find("Ground")))throw std::runtime_error("model must has a part named \"Ground\"");
		}
		void Model::saveXml(const std::string &filename) const
		{
			Aris::Core::XmlDocument doc;

			saveXml(doc);

			doc.SaveFile(filename.c_str());
		}
		void Model::saveXml(Aris::Core::XmlDocument &xml_doc)const
		{
			xml_doc.DeleteChildren();
			
			auto pHeader = xml_doc.NewDeclaration("xml version=\"1.0\" encoding=\"UTF-8\" ");
			xml_doc.InsertFirstChild(pHeader);

			auto pModel = xml_doc.NewElement("");
			saveXml(*pModel);
		}
		void Model::saveXml(Aris::Core::XmlElement &xml_ele)const
		{
			xml_ele.SetName(this->name().c_str());
			xml_ele.DeleteChildren();

			auto pEnvironment = xml_ele.GetDocument()->NewElement("");
			environment().saveXml(*pEnvironment);
			xml_ele.InsertEndChild(pEnvironment);

			auto pVar = xml_ele.GetDocument()->NewElement("Variable");
			xml_ele.InsertEndChild(pVar);

			auto pPrt = xml_ele.GetDocument()->NewElement("Part");
			xml_ele.InsertEndChild(pPrt);

			auto pJnt = xml_ele.GetDocument()->NewElement("Joint");
			xml_ele.InsertEndChild(pJnt);

			auto pMot = xml_ele.GetDocument()->NewElement("Motion");
			xml_ele.InsertEndChild(pMot);

			auto pFce = xml_ele.GetDocument()->NewElement("Force");
			xml_ele.InsertEndChild(pFce);

			for (auto &p : part_pool_)
			{
				auto ele = xml_ele.GetDocument()->NewElement("");
				p->saveXml(*ele);
				pPrt->InsertEndChild(ele);
			}

			for (auto &j : joint_pool_)
			{
				auto ele = xml_ele.GetDocument()->NewElement("");
				j->saveXml(*ele);
				pJnt->InsertEndChild(ele);
			}

			for (auto &m : motion_pool_)
			{
				auto ele = xml_ele.GetDocument()->NewElement("");
				m->saveXml(*ele);
				pMot->InsertEndChild(ele);
			}
			


		}
		void Model::saveAdams(const std::string &filename, bool using_script) const
		{
			std::string filename_ = filename;
			if (filename_.size() < 4 || filename_.substr(filename.size() - 4, 4) != ".cmd")
			{
				filename_ += ".cmd";
			}
			
			std::ofstream file;
			file.open(filename_, std::ios::out | std::ios::trunc);
			file << std::setprecision(15);

			file << "!----------------------------------- Environment -------------------------------!\r\n!\r\n!\r\n";
			environment().saveAdams(file);

			file << "!----------------------------------- Parts -------------------------------------!\r\n!\r\n!\r\n";
			for (auto &prt : part_pool_)
			{
				prt->saveAdams(file);
			}

			file << "!----------------------------------- Joints ------------------------------------!\r\n!\r\n!\r\n";
			for (auto &jnt : joint_pool_)
			{
				jnt->saveAdams(file);
			}

			file << "!----------------------------------- Motions -----------------------------------!\r\n!\r\n!\r\n";
			for (auto &mot : motion_pool_)
			{
				mot->saveAdams(file);
			}

			file << "!----------------------------------- Forces ------------------------------------!\r\n!\r\n!\r\n";
			for (auto &fce : force_pool_)
			{
				fce->saveAdams(file);
			}

			if (using_script)
			{
				file << "!----------------------------------- Script ------------------------------------!\r\n!\r\n!\r\n";
				this->script().saveAdams(file);
			}
			else
			{
				file << "!----------------------------------- Motify Active -------------------------------------!\r\n!\r\n!\r\n";
				for (auto &prt : part_pool_)
				{
					if ((prt.get() != ground_) && (!prt->isActive()))
					{
						file << "part attributes  &\r\n"
							<< "    part_name = ." << name() << "." << prt->name() << "  &\r\n"
							<< "    active = off \r\n!\r\n";
					}
				}
				for (auto &jnt : joint_pool_)
				{
					if (!jnt->isActive())
					{
						file << "constraint attributes  &\r\n"
							<< "    constraint_name = ." << name() << "." << jnt->name() << "  &\r\n"
							<< "    active = off \r\n!\r\n";
					}
					
				}
				for (auto &mot : motion_pool_)
				{
					if (!mot->isActive())
					{
						file << "constraint attributes  &\r\n"
							<< "    constraint_name = ." << name() << "." << mot->name() << "  &\r\n"
							<< "    active = off \r\n!\r\n";
					}
					
				}
				for (auto &fce : force_pool_)
				{
					if (!fce->isActive())
					{
						file << "force attributes  &\r\n"
							<< "    force_name = ." << name() << "." << fce->name() << "  &\r\n"
							<< "    active = off \r\n!\r\n";
					}
				}
			}
			
			file.close();


		}

		RevoluteJoint::RevoluteJoint(Object &father, const std::string &name, int id, Marker &makI, Marker &makJ)
			: JointBaseDim(father, name, id, makI, makJ)
		{
		}
		RevoluteJoint::RevoluteJoint(Object &father, const Aris::Core::XmlElement &xml_ele, int id)
			: JointBaseDim(father, xml_ele, id)
		{
		}
		void RevoluteJoint::init()
		{
			double loc_cst[6][Dim()];
			std::fill_n(&loc_cst[0][0], 6 * Dim(), 0);

			loc_cst[0][0] = 1;
			loc_cst[1][1] = 1;
			loc_cst[2][2] = 1;
			loc_cst[3][3] = 1;
			loc_cst[4][4] = 1;

			s_tf_n(Dim(), *makI().prtPm(), *loc_cst, csmI());
		}

		TranslationalJoint::TranslationalJoint(Object &father, const std::string &name, int id, Marker &makI, Marker &makJ)
			: JointBaseDim(father, name, id, makI, makJ)
		{
		}
		TranslationalJoint::TranslationalJoint(Object &father, const Aris::Core::XmlElement &xml_ele, int id)
			: JointBaseDim(father, xml_ele, id)
		{
		}
		void TranslationalJoint::init()
		{
			double loc_cst[6][Dim()];
			std::fill_n(static_cast<double*>(*loc_cst), 6 * Dim(), 0);

			loc_cst[0][0] = 1;
			loc_cst[1][1] = 1;
			loc_cst[3][2] = 1;
			loc_cst[4][3] = 1;
			loc_cst[5][4] = 1;

			s_tf_n(Dim(), *makI().prtPm(), *loc_cst, csmI());
		};

		UniversalJoint::UniversalJoint(Object &father, const std::string &name, int id, Marker &makI, Marker &makJ)
			: JointBaseDim(father, name, id, makI, makJ)
		{
		}
		UniversalJoint::UniversalJoint(Object &father, const Aris::Core::XmlElement &xml_ele, int id)
			: JointBaseDim(father, xml_ele, id)
		{
		}
		void UniversalJoint::saveAdams(std::ofstream &file) const
		{
			double pe[6] = { 0, 0, 0, PI / 2, 0, 0 };
			double pe2[6] = { 0, 0, 0, -PI / 2, 0, 0 };
			double pm[4][4], pm2[4][4];

			s_pe2pm(pe, *pm, "213");
			s_pm_dot_pm(*this->makI().prtPm(), *pm, *pm2);
			s_pm2pe(*pm2, pe, "313");

			file << "marker modify &\r\n"
				<< "    marker_name = ." << model().name() << "." << this->makI().fatherPart().name() << "." << this->makI().name() << " &\r\n"
				<< "    orientation = (" << Core::Matrix(1, 3, &pe[3]).toString() << ") \r\n"
				<< "!\r\n";

			s_pe2pm(pe2, *pm, "123");
			s_pm_dot_pm(*this->makJ().prtPm(), *pm, *pm2);
			s_pm2pe(*pm2, pe, "313");

			file << "marker modify &\r\n"
				<< "    marker_name = ." << model().name() << "." << this->makJ().fatherPart().name() << "." << this->makJ().name() << " &\r\n"
				<< "    orientation = (" << Core::Matrix(1, 3, &pe[3]).toString() << ") \r\n"
				<< "!\r\n";

			Constraint::saveAdams(file);
		}
		void UniversalJoint::init()
		{
			double loc_cst[6][Dim()];
			std::fill_n(static_cast<double*>(*loc_cst), 6 * Dim(), 0);

			loc_cst[0][0] = 1;
			loc_cst[1][1] = 1;
			loc_cst[2][2] = 1;

			s_tf_n(Dim(), *makI().prtPm(), *loc_cst, csmI());

		};
		void UniversalJoint::update()
		{
			/*update PrtCstMtxI*/
			makI().update();
			makJ().update();

			//get sin(a) and cos(a)
			double s = makI().pm()[0][2] * makJ().pm()[0][1]
				+ makI().pm()[1][2] * makJ().pm()[1][1]
				+ makI().pm()[2][2] * makJ().pm()[2][1];

			double c = makI().pm()[0][1] * makJ().pm()[0][1]
				+ makI().pm()[1][1] * makJ().pm()[1][1]
				+ makI().pm()[2][1] * makJ().pm()[2][1];

			csmI_[3][3] = -(makI().prtPm()[0][1]) * s + (makI().prtPm()[0][2]) * c;
			csmI_[4][3] = -(makI().prtPm()[1][1]) * s + (makI().prtPm()[1][2]) * c;
			csmI_[5][3] = -(makI().prtPm()[2][1]) * s + (makI().prtPm()[2][2]) * c;

			/*edit CstMtxJ*/
			std::fill_n(static_cast<double *>(csmJ()), this->dim() * 6, 0);
			double pm_M2N[4][4];
			s_pm_dot_pm(*makJ().fatherPart().invPm(), *makI().fatherPart().pm(), *pm_M2N);
			s_tf_n(Dim(), -1, *pm_M2N, csmI(), 0, csmJ());



			/*update A_c*/
			std::fill_n(csa(), UniversalJoint::dim(), 0);

			/*calculate a_dot*/
			double v[3];
			v[0] = makJ().vel()[3] - makI().vel()[3];
			v[1] = makJ().vel()[4] - makI().vel()[4];
			v[2] = makJ().vel()[5] - makI().vel()[5];

			double a_dot = makI().pm()[0][0] * v[0] + makI().pm()[1][0] * v[1] + makI().pm()[2][0] * v[2];

			/*calculate part m*/
			v[0] = -c*a_dot;
			v[1] = -s*a_dot;

			double tem_v1[6]{ 0 }, tem_v2[6]{ 0 };
			s_inv_tv(*makI().prtPm(), makI().fatherPart().prtVel(), tem_v1);
			csa()[3] -= v[0] * tem_v1[4] + v[1] * tem_v1[5];

			/*calculate part n*/
			s_inv_tv(*pm_M2N, makJ().fatherPart().prtVel(), tem_v1);
			s_cv(-1, makI().fatherPart().prtVel(), tem_v1, 0, tem_v2);
			s_dgemmTN(4, 1, 6, 1, csmI(), Dim(), tem_v2, 1, 1, &csa()[0], 1);
			s_inv_tv(*makI().prtPm(), tem_v1, tem_v2);
			csa()[3] += v[0] * tem_v2[4] + v[1] * tem_v2[5];
		};

		SphericalJoint::SphericalJoint(Object &father, const std::string &name, int id, Marker &makI, Marker &makJ)
			: JointBaseDim(father, name, id, makI, makJ)
		{
		}
		SphericalJoint::SphericalJoint(Object &father, const Aris::Core::XmlElement &xml_ele, int id)
			: JointBaseDim(father, xml_ele, id)
		{
		}
		void SphericalJoint::init()
		{
			double loc_cst[6][Dim()];
			std::fill_n(static_cast<double*>(*loc_cst), 6 * Dim(), 0);

			loc_cst[0][0] = 1;
			loc_cst[1][1] = 1;
			loc_cst[2][2] = 1;

			s_tf_n(Dim(), *makI().prtPm(), *loc_cst, csmI());
		};

		SingleComponentMotion::SingleComponentMotion(Object &father, const std::string &name, int id, Marker &makI, Marker &makJ, int component_axis)
			: Motion(father, name, id, makI, makJ), component_axis_(component_axis)
		{
		}
		SingleComponentMotion::SingleComponentMotion(Object &father, const Aris::Core::XmlElement &xml_ele, int id)
			: Motion(father, xml_ele, id)
		{
			component_axis_ = std::stoi(xml_ele.Attribute("Component"));
		}
		void SingleComponentMotion::init()
		{
			double loc_cst[6]{ 0 };

			std::fill_n(csmI(), 6, 0);
			std::fill_n(csmJ(), 6, 0);

			/* Get tm I2M */
			loc_cst[component_axis_] = 1;
			s_tf(*makI().prtPm(), loc_cst, csmI());
		}
		void SingleComponentMotion::update()
		{
			/*update motPos motVel,  motAcc should be given, not computed by part acc*/
			makI().update();
			makJ().update();

			double pm_I2J[4][4], pe[6];
			s_inv_pm_dot_pm(*makJ().pm(), *makI().pm(), *pm_I2J);
			s_pm2pe(&pm_I2J[0][0], pe, "123");
			mot_pos_ = pe[component_axis_];

			double velDiff[6], velDiff_in_J[6];
			std::copy_n(makI().vel(), 6, velDiff);
			s_daxpy(6, -1, makJ().vel(), 1, velDiff, 1);
			s_inv_tv(*makJ().pm(), velDiff, velDiff_in_J);
			mot_vel_ = velDiff_in_J[component_axis_];

			/*update cst mtx*/
			std::fill_n(csmJ(), 6, 0);
			double pm_M2N[4][4];
			s_pm_dot_pm(*makJ().fatherPart().invPm(), *makI().fatherPart().pm(), *pm_M2N);
			s_tf(-1, *pm_M2N, csmI(), 0, csmJ());

			/*update a_c*/
			std::fill_n(csa(), 1, 0);
			double tem_v1[6]{ 0 }, tem_v2[6]{ 0 };
			s_inv_tv(-1, *pm_M2N, makJ().fatherPart().prtVel(), 0, tem_v1);
			s_cv(makI().fatherPart().prtVel(), tem_v1, tem_v2);
			s_dgemmTN(1, 1, 6, 1, csmI(), 1, tem_v2, 1, 0, csa(), 1);

			csa()[0] += mot_acc_;
			/*update motPos motVel motAcc*/
		}
		void SingleComponentMotion::saveAdams(std::ofstream &file) const
		{
			std::string s;

			switch (component_axis_)
			{
			case 0:
				s = "x";
				break;
			case 1:
				s = "y";
				break;
			case 2:
				s = "z";
				break;
			case 3:
				s = "B1";
				break;
			case 4:
				s = "B2";
				break;
			case 5:
				s = "B3";
				break;
			}


			if (this->posCurve == nullptr)
			{
				file << "constraint create motion_generator &\r\n"
					<< "    motion_name = ." << model().name() << "." << this->name() << "  &\r\n"
					<< "    adams_id = " << adamsID() << "  &\r\n"
					<< "    i_marker_name = ." << model().name() << "." << this->makI().fatherPart().name() << "." << this->makI().name() << "  &\r\n"
					<< "    j_marker_name = ." << model().name() << "." << this->makJ().fatherPart().name() << "." << this->makJ().name() << "  &\r\n"
					<< "    axis = " << s << "  &\r\n"
					<< "    function = \"" << this->motPos() << "\"  \r\n"
					<< "!\r\n";
			}
			else
			{
				file << "data_element create spline &\r\n"
					<< "    spline_name = ." << model().name() << "." << this->name() << "_pos_spl  &\r\n"
					<< "    adams_id = " << adamsID() * 2 - 1 << "  &\r\n"
					<< "    units = m &\r\n"
					<< "    x = " << this->posCurve->x().at(0);
				for (auto p = this->posCurve->x().begin() + 1; p < this->posCurve->x().end(); ++p)
				{
					file << "," << *p;
				}
				file << "    y = " << this->posCurve->y().at(0);
				for (auto p = this->posCurve->y().begin() + 1; p < this->posCurve->y().end(); ++p)
				{
					file << "," << *p;
				}
				file << " \r\n!\r\n";

				file << "constraint create motion_generator &\r\n"
					<< "    motion_name = ." << model().name() << "." << this->name() << "  &\r\n"
					<< "    adams_id = " << adamsID() << "  &\r\n"
					<< "    i_marker_name = ." << model().name() << "." << this->makI().fatherPart().name() << "." << this->makI().name() << "  &\r\n"
					<< "    j_marker_name = ." << model().name() << "." << this->makJ().fatherPart().name() << "." << this->makJ().name() << "  &\r\n"
					<< "    axis = " << s << "  &\r\n"
					<< "    function = \"AKISPL(time,0," << this->name() << "_pos_spl)\"  \r\n"
					<< "!\r\n";
			}
		}

		void SingleComponentForce::update()
		{
			s_tf(*makI().prtPm(), fce_value_, fceI_);
			double pm_M2N[16];
			s_inv_pm_dot_pm(*makJ().fatherPart().pm(), *makI().fatherPart().pm(), pm_M2N);
			s_tf(-1, pm_M2N, fceI_, 0, fceJ_);
		}
		SingleComponentForce::SingleComponentForce(Object &father, const std::string &name, int id, Marker& makI, Marker& makJ, int componentID)
			: Force(father, name, id, makI, makJ), component_axis_(componentID)
		{

		}
		SingleComponentForce::SingleComponentForce(Object &father, const Aris::Core::XmlElement &xml_ele, int id)
			: Force(father, xml_ele, id), component_axis_(std::stoi(xml_ele.Attribute("Component")))
		{
		}
		void SingleComponentForce::saveAdams(std::ofstream &file) const
		{
			if (fce_akima_ == nullptr)
			{
				std::string type = "translational";

				file << "force create direct single_component_force  &\r\n"
					<< "    single_component_force_name = ." << model().name() << "." << name() << "  &\r\n"
					<< "    adams_id = " << adamsID() << "  &\r\n"
					<< "    type_of_freedom = " << type << "  &\r\n"
					<< "    i_marker_name = ." << model().name() << "." << makI().fatherPart().name() << "." << makI().name() << "  &\r\n"
					<< "    j_marker_name = ." << model().name() << "." << makJ().fatherPart().name() << "." << makJ().name() << "  &\r\n"
					<< "    action_only = off  &\r\n"
					<< "    function = \"" << fce() << "\"  \r\n"
					<< "!\r\n";
			}
			else
			{
				std::string type = "translational";

				file << "data_element create spline &\r\n"
					<< "    spline_name = ." << model().name() << "." << name() << "_fce_spl  &\r\n"
					<< "    adams_id = " << adamsID() * 2 << "  &\r\n"
					<< "    units = N &\r\n"
					<< "    x = " << fce_akima_->x().at(0);
				for (auto p = fce_akima_->x().begin() + 1; p < fce_akima_->x().end(); ++p)
				{
					file << "," << *p;
				}
				file << "    y = " << fce_akima_->y().at(0);
				for (auto p = fce_akima_->y().begin() + 1; p < fce_akima_->y().end(); ++p)
				{
					file << "," << *p;
				}
				file << " \r\n!\r\n";

				file << "force create direct single_component_force  &\r\n"
					<< "    single_component_force_name = ." << model().name() << "." << name() << "  &\r\n"
					<< "    adams_id = " << adamsID() << "  &\r\n"
					<< "    type_of_freedom = " << type << "  &\r\n"
					<< "    i_marker_name = ." << model().name() << "." << makI().fatherPart().name() << "." << makI().name() << "  &\r\n"
					<< "    j_marker_name = ." << model().name() << "." << makJ().fatherPart().name() << "." << makJ().name() << "  &\r\n"
					<< "    action_only = off  &\r\n"
					<< "    function = \"AKISPL(time,0," << name() << "_fce_spl)\"  \r\n"
					<< "!\r\n";
			}



		}

	}
}