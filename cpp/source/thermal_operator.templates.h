/* Copyright (c) 2016, the Cap authors.
 *
 * This file is subject to the Modified BSD License and may not be distributed
 * without copyright and license information. Please refer to the file LICENSE
 * for the text and further information on this license. 
 */

#include <cap/thermal_operator.h>
#include <cap/dof_extractor.h>
#include <deal.II/fe/fe_values.h>
#include <deal.II/base/quadrature_lib.h>
#include <deal.II/base/function.h>
#include <deal.II/dofs/dof_tools.h>
#include <deal.II/numerics/vector_tools.h>
#include <numeric>
#include <tuple>

namespace cap
{

template <int dim>
ThermalOperator<dim>::ThermalOperator(
    std::shared_ptr<OperatorParameters<dim> const> parameters)
    : Operator<dim>(parameters)
{
  std::shared_ptr<boost::property_tree::ptree const> database =
      parameters->database;
  this->temperature_component =
      database->get<unsigned int>("temperature_component");

  dealii::DoFHandler<dim> const &dof_handler = *(this->dof_handler);
  unsigned int const n_components = dealii::DoFTools::n_components(dof_handler);
  std::vector<dealii::types::global_dof_index> dofs_per_component(n_components);
  dealii::DoFTools::count_dofs_per_component(dof_handler, dofs_per_component);
  this->dof_shift =
      std::accumulate(&(dofs_per_component[0]),
                      &(dofs_per_component[this->temperature_component]), 0);

  std::shared_ptr<ThermalOperatorParameters<dim> const> thermal_parameters =
      std::dynamic_pointer_cast<ThermalOperatorParameters<dim> const>(
          parameters);
}

template <int dim>
void ThermalOperator<dim>::reset(
    std::shared_ptr<OperatorParameters<dim> const> parameters)
{
  std::ignore = parameters;
  // std::cout<<"### reset thermal ###\n";
  this->stiffness_matrix = 0.0;
  this->mass_matrix      = 0.0;
  this->load_vector = 0.0;
  this->boundary_values.clear();

  this->compute_thermal_operator_contribution();
  this->compute_robin_boundary_contribution();

  // std::cout<<std::setprecision(15)
  //    <<"stiffness="<<this->stiffness_matrix.l1_norm()<<"  "
  //    <<"mass="<<this->mass_matrix.l1_norm()<<"  "
  //    <<"load="<<this->load_vector.l2_norm()<<"\n";
}

template <int dim>
void ThermalOperator<dim>::compute_thermal_operator_contribution()
{
  dealii::DoFHandler<dim> const &dof_handler = *(this->dof_handler);
  dealii::ConstraintMatrix const &constraint_matrix =
      *(this->constraint_matrix);
  dealii::FEValuesExtractors::Scalar const temperature(
      this->temperature_component);
  dealii::FiniteElement<dim> const &fe = dof_handler.get_fe();
  dealii::QGauss<dim> quadrature_rule(fe.degree + 1);
  dealii::FEValues<dim> fe_values(
      fe, quadrature_rule, dealii::update_values | dealii::update_gradients |
                               dealii::update_JxW_values);
  unsigned int const dofs_per_cell = fe.dofs_per_cell;
  unsigned int const n_q_points = quadrature_rule.size();
  dealii::FullMatrix<double> cell_stiffness_matrix(dofs_per_cell,
                                                   dofs_per_cell);
  dealii::FullMatrix<double> cell_mass_matrix(dofs_per_cell, dofs_per_cell);
  std::vector<double> density_times_heat_capacity_values(n_q_points);
  std::vector<double> thermal_conductivity_values(n_q_points);
  std::vector<dealii::types::global_dof_index> local_dof_indices(dofs_per_cell);
  unsigned int const n_components = dealii::DoFTools::n_components(dof_handler);
  dealii::ComponentMask the_mask(n_components, false);
  the_mask.set(this->temperature_component, true);
  DoFExtractor dof_extractor(the_mask, the_mask, dofs_per_cell);
  typename dealii::DoFHandler<dim>::active_cell_iterator
      cell     = dof_handler.begin_active(),
      end_cell = dof_handler.end();
  for (; cell != end_cell; ++cell)
  {
    cell_stiffness_matrix = 0.0;
    cell_mass_matrix = 0.0;
    fe_values.reinit(cell);
    (this->mp_values)
        ->get_values("thermal_conductivity", cell, thermal_conductivity_values);
    (this->mp_values)
        ->get_values("density_times_heat_capacity", cell,
                     density_times_heat_capacity_values);
    for (unsigned int q_point = 0; q_point < n_q_points; ++q_point)
    {
      for (unsigned int i = 0; i < dofs_per_cell; ++i)
      {
        for (unsigned int j = 0; j < dofs_per_cell; ++j)
        {
          cell_stiffness_matrix(i, j) +=
              thermal_conductivity_values[q_point] *
              (fe_values[temperature].gradient(i, q_point) *
               fe_values[temperature].gradient(j, q_point)) *
              fe_values.JxW(q_point);
          cell_mass_matrix(i, j) +=
              density_times_heat_capacity_values[q_point] *
              (fe_values[temperature].value(i, q_point) *
               fe_values[temperature].value(j, q_point)) *
              fe_values.JxW(q_point);
        } // end for j
      }   // end for i
    }     // end for quadrature point
    cell->get_dof_indices(local_dof_indices);
    std::vector<dealii::types::global_dof_index> tmp_indices =
        dof_extractor.extract_row_indices(local_dof_indices);
    dealii::FullMatrix<double> tmp_mass_matrix =
        dof_extractor.extract_matrix(cell_mass_matrix);
    dealii::FullMatrix<double> tmp_stiffness_matrix =
        dof_extractor.extract_matrix(cell_stiffness_matrix);
    if (this->dof_shift != 0)
      std::transform(tmp_indices.begin(), tmp_indices.end(),
                     tmp_indices.begin(),
                     std::bind2nd(std::minus<dealii::types::global_dof_index>(),
                                  this->dof_shift));
    constraint_matrix.distribute_local_to_global(
        tmp_stiffness_matrix, tmp_indices, this->stiffness_matrix);
    constraint_matrix.distribute_local_to_global(tmp_mass_matrix, tmp_indices,
                                                 this->mass_matrix);
  } // end for cell
}

template <int dim>
void ThermalOperator<dim>::compute_robin_boundary_contribution()
{
  dealii::DoFHandler<dim> const &dof_handler = *(this->dof_handler);
  dealii::ConstraintMatrix const &constraint_matrix =
      *(this->constraint_matrix);
  dealii::FEValuesExtractors::Scalar const temperature(
      this->temperature_component);
  dealii::FiniteElement<dim> const &fe = dof_handler.get_fe();
  dealii::QGauss<dim> quadrature_rule(fe.degree + 1);
  dealii::QGauss<dim - 1> face_quadrature_rule(fe.degree + 1);
  dealii::FEFaceValues<dim> fe_face_values(fe, face_quadrature_rule,
                                           dealii::update_values |
                                               dealii::update_JxW_values);
  unsigned int const dofs_per_cell   = fe.dofs_per_cell;
  unsigned int const n_face_q_points = face_quadrature_rule.size();
  dealii::FullMatrix<double> cell_stiffness_matrix(dofs_per_cell,
                                                   dofs_per_cell);
  dealii::Vector<double> cell_load_vector(dofs_per_cell);
  std::vector<double> heat_transfer_coefficient_values(n_face_q_points);
  std::vector<double> ambient_temperature_values(n_face_q_points);
  std::vector<dealii::types::global_dof_index> local_dof_indices(dofs_per_cell);
  unsigned int const n_components = dealii::DoFTools::n_components(dof_handler);
  dealii::ComponentMask the_mask(n_components, false);
  the_mask.set(this->temperature_component, true);
  DoFExtractor dof_extractor(the_mask, the_mask, dofs_per_cell);
  typename dealii::DoFHandler<dim>::active_cell_iterator
      cell     = dof_handler.begin_active(),
      end_cell = dof_handler.end();
  for (; cell != end_cell; ++cell)
  {
    cell_stiffness_matrix = 0.0;
    cell_load_vector = 0.0;
    if (cell->at_boundary())
    {
      for (unsigned int face = 0;
           face < dealii::GeometryInfo<dim>::faces_per_cell; ++face)
      {
        if (cell->face(face)->at_boundary())
        {
          (this->b_values)
              ->get_values("heat_transfer_coefficient", cell, face,
                           heat_transfer_coefficient_values);
          (this->b_values)
              ->get_values("ambient_temperature", cell, face,
                           ambient_temperature_values);
          fe_face_values.reinit(cell, face);
          for (unsigned int q_point = 0; q_point < n_face_q_points; ++q_point)
          {
            for (unsigned int i = 0; i < dofs_per_cell; ++i)
            {
              for (unsigned int j = 0; j < dofs_per_cell; ++j)
              {
                cell_stiffness_matrix(i, j) +=
                    heat_transfer_coefficient_values[q_point] *
                    fe_face_values[temperature].value(i, q_point) *
                    fe_face_values[temperature].value(j, q_point) *
                    fe_face_values.JxW(q_point);
              } // end for j
              cell_load_vector(i) +=
                  heat_transfer_coefficient_values[q_point] *
                  ambient_temperature_values[q_point] *
                  fe_face_values[temperature].value(i, q_point) *
                  fe_face_values.JxW(q_point);
            } // end for i
          }   // end for face quadrature point
        }     // end if
      }       // end for face
    }         // end if cell at boundary
    cell->get_dof_indices(local_dof_indices);
    std::vector<dealii::types::global_dof_index> tmp_indices =
        dof_extractor.extract_row_indices(local_dof_indices);
    dealii::FullMatrix<double> tmp_stiffness_matrix =
        dof_extractor.extract_matrix(cell_stiffness_matrix);
    dealii::Vector<double> tmp_load_vector =
        dof_extractor.extract_vector(cell_load_vector);
    if (this->dof_shift != 0)
      std::transform(tmp_indices.begin(), tmp_indices.end(),
                     tmp_indices.begin(),
                     std::bind2nd(std::minus<dealii::types::global_dof_index>(),
                                  this->dof_shift));
    constraint_matrix.distribute_local_to_global(
        tmp_stiffness_matrix, tmp_indices, this->stiffness_matrix);
    constraint_matrix.distribute_local_to_global(tmp_load_vector, tmp_indices,
                                                 this->load_vector);
  } // end for cell
}

} // end namespace cap
